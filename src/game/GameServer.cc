#include "game/GameServer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "proto/MsgId.h"

namespace gs::game {

using net::TcpConnectionPtr;

GameServer::GameServer(net::EventLoop* baseLoop, uint16_t port, int ioThreads,
                       int gameThreads, StorageOptions storeOpts)
    : gameThreads_(gameThreads),
      server_(baseLoop, net::InetAddress(port), "gameserver"),
      codec_([this](const TcpConnectionPtr& c, uint16_t id, const char* d,
                    size_t n) { dispatcher_.onRawMessage(c, id, d, n); }),
      storeOpts_(std::move(storeOpts)) {
  server_.setThreadNum(ioThreads);
  server_.setMessageCallback([this](const TcpConnectionPtr& c,
                                    net::Buffer* b) { codec_.onMessage(c, b); });
  server_.setConnectionCallback([this](const TcpConnectionPtr& c) {
    if (!c->connected()) {
      // IO 线程 → lobby:连接断开只带 connName,session 是否存在 lobby 判断
      std::string name = c->name();
      lobby_->runInLoop([this, name] { onDisconnect(name); });
    }
  });

  // 所有 handler:IO 线程解析 → 值拷贝 hop 到 lobby 线程
  dispatcher_.registerHandler<pb::LoginReq>(
      proto::kLoginReq, [this](const TcpConnectionPtr& c, const pb::LoginReq& m) {
        lobby_->runInLoop([this, c, m] { onLogin(c, m); });
      });
  dispatcher_.registerHandler<pb::ReconnectReq>(
      proto::kReconnectReq,
      [this](const TcpConnectionPtr& c, const pb::ReconnectReq& m) {
        lobby_->runInLoop([this, c, m] { onReconnect(c, m); });
      });
  dispatcher_.registerHandler<pb::MatchReq>(
      proto::kMatchReq, [this](const TcpConnectionPtr& c, const pb::MatchReq& m) {
        lobby_->runInLoop([this, c, m] { onMatch(c, m); });
      });
  dispatcher_.registerHandler<pb::PlayerInput>(
      proto::kPlayerInput,
      [this](const TcpConnectionPtr& c, const pb::PlayerInput& m) {
        lobby_->runInLoop([this, c, m] { onInput(c, m); });
      });
  dispatcher_.registerHandler<pb::Heartbeat>(
      proto::kHeartbeat,
      [this](const TcpConnectionPtr& c, const pb::Heartbeat& m) {
        lobby_->runInLoop([this, c, m] { onHeartbeat(c, m); });
      });
  // 排行榜查询不经 lobby:直接进存储队列,查完在存储线程回包
  dispatcher_.registerHandler<pb::LeaderboardReq>(
      proto::kLeaderboardReq,
      [this](const TcpConnectionPtr& c, const pb::LeaderboardReq& m) {
        onLeaderboard(c, m);
      });
}

GameServer::~GameServer() {
  if (!lobby_) return;
  // 每个定时器/Channel 都必须在自己所属 loop 的线程销毁:
  // lobby 上销毁 wheelDriver;每个房间在其 game loop 上停 ticker;
  // 全部完成后在 lobby 上关闭 game 线程池,再放行主线程。
  std::promise<void> done;
  lobby_->runInLoop([this, &done] {
    wheelDriver_.reset();
    auto rooms = std::move(rooms_);
    rooms_.clear();
    auto remain = std::make_shared<std::atomic<int>>(
        static_cast<int>(rooms.size()));
    auto finishOne = [this, &done, remain] {
      if (remain->fetch_sub(1) == 1) {
        lobby_->runInLoop([this, &done] {
          gamePool_.reset();
          done.set_value();
        });
      }
    };
    if (rooms.empty()) {
      gamePool_.reset();
      done.set_value();
      return;
    }
    for (auto& [id, r] : rooms) {
      RoomPtr room = r;
      room->loop()->runInLoop([room, finishOne] {
        room->stop();
        finishOne();
      });
    }
  });
  done.get_future().wait();
}

void GameServer::start() {
  if (storeOpts_.enabled) {
    writer_ = std::make_unique<storage::AsyncWriter>();
    // 连接动作也排进存储队列:客户端对象从生到死只被存储线程触碰
    writer_->enqueue([this] {
      bool ok = redis_.connect(storeOpts_.redisHost, storeOpts_.redisPort) &&
                redis_.ping() &&
                mysql_.connect(storeOpts_.mysqlHost, storeOpts_.mysqlUser,
                               storeOpts_.mysqlPass, storeOpts_.mysqlDb) &&
                mysql_.ensureSchema();
      storageReady_ = ok;
      std::printf("[storage] %s\n", ok ? "ready" : "unavailable, degraded");
    });
  }
  lobby_ = lobbyThread_.startLoop();
  lobby_->runInLoop([this] {
    // game 线程池以 lobby 为 base(0 线程时房间退化跑在 lobby 上)
    gamePool_ = std::make_unique<net::EventLoopThreadPool>(lobby_);
    gamePool_->start(gameThreads_);
    // 100ms 驱动时间轮
    wheelDriver_ = std::make_unique<timer::PeriodicTimer>(
        lobby_, 100, [this] { wheel_.advance(100); });
    scheduleSweep();
  });
  server_.start();
}

// —— lobby 线程 ——

void GameServer::onLogin(const TcpConnectionPtr& conn, const pb::LoginReq& req) {
  auto s = std::make_shared<Session>();
  s->playerId = nextPlayerId_++;
  s->token = genToken();
  s->account = req.account();
  s->conn = conn;
  s->lastHeartbeatMs = nowMs();
  sessions_[s->playerId] = s;
  connToPlayer_[conn->name()] = s->playerId;

  pb::LoginResp resp;
  resp.set_code(0);
  resp.set_player_id(s->playerId);
  resp.set_session_token(s->token);
  codec_.send(conn, proto::kLoginResp, resp);

  if (writer_) {  // 在线状态异步落 Redis
    std::string acc = s->account;
    writer_->enqueue([this, acc] {
      if (storageReady_) redis_.set("online:" + acc, "1");
    });
  }
}

void GameServer::onReconnect(const TcpConnectionPtr& conn,
                             const pb::ReconnectReq& req) {
  auto it = sessions_.find(req.player_id());
  if (it == sessions_.end() || it->second->token != req.session_token() ||
      it->second->conn != nullptr) {  // 不存在/token 错/旧连接还活着
    pb::LoginResp resp;
    resp.set_code(1);
    codec_.send(conn, proto::kLoginResp, resp);
    conn->shutdown();
    return;
  }
  SessionPtr s = it->second;
  if (s->expireTimerId) {  // 撤销 30s 销毁定时器
    wheel_.cancel(s->expireTimerId);
    s->expireTimerId = 0;
  }
  s->conn = conn;
  s->lastHeartbeatMs = nowMs();
  connToPlayer_[conn->name()] = s->playerId;

  pb::LoginResp resp;
  resp.set_code(0);
  resp.set_player_id(s->playerId);
  resp.set_session_token(s->token);
  codec_.send(conn, proto::kLoginResp, resp);

  if (s->state == Session::kInRoom) {  // 回到战场:房间线程补上下文
    auto rit = rooms_.find(s->roomId);
    if (rit != rooms_.end()) {
      RoomPtr room = rit->second;
      uint64_t pid = s->playerId;
      room->loop()->runInLoop([room, pid, conn] { room->rebind(pid, conn); });
    }
  }
  if (writer_) {  // 重新上线
    std::string acc = s->account;
    writer_->enqueue([this, acc] {
      if (storageReady_) redis_.set("online:" + acc, "1");
    });
  }
}

void GameServer::onMatch(const TcpConnectionPtr& conn, const pb::MatchReq&) {
  SessionPtr s = sessionOf(conn);
  if (!s || s->state != Session::kLobby) return;
  if (!matchQueue_.push(s->playerId)) return;
  s->state = Session::kMatching;
  // 30s 匹配超时:出队并用 room_id=0 的 EnterRoom 通知失败
  uint64_t pid = s->playerId;
  matchTimer_[pid] = wheel_.add(kMatchTimeoutMs, [this, pid] {
    matchTimer_.erase(pid);
    if (!matchQueue_.contains(pid)) return;
    matchQueue_.remove(pid);
    auto sit = sessions_.find(pid);
    if (sit == sessions_.end()) return;
    sit->second->state = Session::kLobby;
    if (sit->second->conn) {
      pb::EnterRoom fail;  // room_id=0 约定为匹配失败
      fail.set_room_id(0);
      codec_.send(sit->second->conn, proto::kEnterRoom, fail);
    }
  });
  tryMatch();
}

void GameServer::tryMatch() {
  auto pair = matchQueue_.popPair();
  if (!pair) return;
  auto sa = sessions_.find(pair->first);
  auto sb = sessions_.find(pair->second);
  if (sa == sessions_.end() || sb == sessions_.end()) return;
  for (uint64_t pid : {pair->first, pair->second}) {  // 撤匹配超时定时器
    auto t = matchTimer_.find(pid);
    if (t != matchTimer_.end()) {
      wheel_.cancel(t->second);
      matchTimer_.erase(t);
    }
  }
  uint64_t roomId = nextRoomId_++;
  net::EventLoop* gameLoop = gamePool_->getNextLoop();
  auto room = std::make_shared<Room>(
      gameLoop, roomId,
      std::vector<std::pair<uint64_t, TcpConnectionPtr>>{
          {sa->second->playerId, sa->second->conn},
          {sb->second->playerId, sb->second->conn}},
      &codec_, [this](uint64_t rid, uint64_t winner) {
        // room loop → lobby
        lobby_->runInLoop([this, rid, winner] { onRoomEnd(rid, winner); });
      });
  rooms_[roomId] = room;
  for (auto* sp : {&sa->second, &sb->second}) {
    (*sp)->state = Session::kInRoom;
    (*sp)->roomId = roomId;
  }
  gameLoop->runInLoop([room] { room->start(); });
  std::printf("[lobby] room %lu: %lu vs %lu\n",
              static_cast<unsigned long>(roomId),
              static_cast<unsigned long>(pair->first),
              static_cast<unsigned long>(pair->second));
}

void GameServer::onInput(const TcpConnectionPtr& conn, const pb::PlayerInput& in) {
  SessionPtr s = sessionOf(conn);
  if (!s || s->state != Session::kInRoom) return;
  auto rit = rooms_.find(s->roomId);
  if (rit == rooms_.end()) return;
  RoomPtr room = rit->second;
  uint64_t pid = s->playerId;
  room->loop()->runInLoop([room, pid, in] { room->onInput(pid, in); });
}

void GameServer::onHeartbeat(const TcpConnectionPtr& conn, const pb::Heartbeat& hb) {
  SessionPtr s = sessionOf(conn);
  if (s) s->lastHeartbeatMs = nowMs();
  codec_.send(conn, proto::kHeartbeat, hb);  // 原样回发,客户端算 RTT
}

void GameServer::onDisconnect(const std::string& connName) {
  auto it = connToPlayer_.find(connName);
  if (it == connToPlayer_.end()) return;
  uint64_t pid = it->second;
  connToPlayer_.erase(it);
  auto sit = sessions_.find(pid);
  if (sit == sessions_.end()) return;
  SessionPtr s = sit->second;
  s->conn.reset();
  if (s->state == Session::kMatching) {  // 排队中断线:直接出队回大厅
    matchQueue_.remove(pid);
    s->state = Session::kLobby;
  }
  if (s->state == Session::kInRoom) {  // 战斗中断线:房间解绑,保留 session
    auto rit = rooms_.find(s->roomId);
    if (rit != rooms_.end()) {
      RoomPtr room = rit->second;
      room->loop()->runInLoop([room, pid] { room->dropConn(pid); });
    }
  }
  // 30s 内可重连,到期销毁
  s->expireTimerId =
      wheel_.add(kSessionKeepMs, [this, pid] { expireSession(pid); });

  if (writer_) {  // 下线状态异步清 Redis
    std::string acc = s->account;
    writer_->enqueue([this, acc] {
      if (storageReady_) redis_.del("online:" + acc);
    });
  }
}

void GameServer::expireSession(uint64_t pid) {
  auto sit = sessions_.find(pid);
  if (sit == sessions_.end() || sit->second->conn) return;  // 已重连
  SessionPtr s = sit->second;
  if (s->state == Session::kInRoom) {  // 弃赛:对手直接获胜
    auto rit = rooms_.find(s->roomId);
    if (rit != rooms_.end()) {
      RoomPtr room = rit->second;
      uint64_t winner = 0;
      for (auto& [opid, os] : sessions_) {
        if (os->roomId == s->roomId && opid != pid) winner = opid;
      }
      room->loop()->runInLoop([room, winner] { room->forceEnd(winner); });
    }
  }
  sessions_.erase(pid);
}

void GameServer::onRoomEnd(uint64_t roomId, uint64_t winnerId) {
  rooms_.erase(roomId);
  std::string winnerAcc, loserAcc;
  for (auto& [pid, s] : sessions_) {
    if (s->roomId == roomId) {
      (pid == winnerId ? winnerAcc : loserAcc) = s->account;
      s->roomId = 0;
      s->state = Session::kLobby;
    }
  }
  // 结算落库:业务线程只入队,存储线程慢慢写(削峰)
  if (writer_ && !winnerAcc.empty() && !loserAcc.empty()) {
    writer_->enqueue([this, winnerAcc, loserAcc] {
      if (!storageReady_) return;
      mysql_.recordResult(winnerAcc, loserAcc);
      redis_.zincrby(storeOpts_.leaderboardKey, winnerAcc, 1);
    });
  }
}

void GameServer::onLeaderboard(const TcpConnectionPtr& conn,
                               const pb::LeaderboardReq& req) {
  int n = static_cast<int>(req.top_n());
  if (n <= 0 || n > 100) n = 10;
  if (!writer_) {  // 存储未启用:回空列表
    codec_.send(conn, proto::kLeaderboardResp, pb::LeaderboardResp());
    return;
  }
  writer_->enqueue([this, conn, n] {
    pb::LeaderboardResp resp;
    if (storageReady_) {
      for (auto& [acc, score] : redis_.ztopN(storeOpts_.leaderboardKey, n)) {
        auto* e = resp.add_entries();
        e->set_account(acc);
        e->set_score(score);
      }
    }
    codec_.send(conn, proto::kLeaderboardResp, resp);  // send 线程安全
  });
}

void GameServer::sweepHeartbeats() {
  uint64_t now = nowMs();
  for (auto& [pid, s] : sessions_) {
    if (s->conn && now - s->lastHeartbeatMs > kHeartbeatTimeoutMs) {
      std::printf("[lobby] kick %lu: heartbeat timeout\n",
                  static_cast<unsigned long>(pid));
      s->conn->shutdown();  // 触发 onDisconnect 走统一断线流程
    }
  }
}

void GameServer::scheduleSweep() {
  wheel_.add(kSweepIntervalMs, [this] {
    sweepHeartbeats();
    scheduleSweep();  // 自续期
  });
}

SessionPtr GameServer::sessionOf(const TcpConnectionPtr& conn) {
  auto it = connToPlayer_.find(conn->name());
  if (it == connToPlayer_.end()) return nullptr;
  auto sit = sessions_.find(it->second);
  return sit == sessions_.end() ? nullptr : sit->second;
}

uint64_t GameServer::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string GameServer::genToken() {
  char buf[33];
  std::snprintf(buf, sizeof buf, "%016llx%016llx",
                static_cast<unsigned long long>(rng_()),
                static_cast<unsigned long long>(rng_()));
  return buf;
}

}  // namespace gs::game
