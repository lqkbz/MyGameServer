// bot 客户端:登录→匹配→追最近敌人+近身攻击→打印战况。
// 用法: ./bot_client <port> <account> [--drop-after-ms N]
//   --drop-after-ms: 进房 N 毫秒后断线,再用保存的 token 重连(演示断线重连)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "game.pb.h"
#include "proto/MsgId.h"
#include "proto/ProtobufCodec.h"
using namespace gs;
using proto::MsgId;
using proto::ProtobufCodec;

namespace {

uint64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int connectTo(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  timeval tv{0, 100 * 1000};  // 100ms 收包超时:兼做输入节拍
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
    std::perror("connect");
    std::exit(1);
  }
  return fd;
}

void sendMsg(int fd, uint16_t id, const google::protobuf::Message& m) {
  std::string f = ProtobufCodec::encode(id, m);
  ::send(fd, f.data(), f.size(), 0);
}

bool recvN(int fd, char* dst, size_t n) {  // 收满 n 字节;超时/断开返回 false
  size_t got = 0;
  while (got < n) {
    ssize_t r = ::recv(fd, dst + got, n - got, 0);
    if (r <= 0) return false;
    got += r;
  }
  return true;
}

bool recvFrame(int fd, uint16_t* id, std::string* body) {
  char head[6];
  if (!recvN(fd, head, 6)) return false;
  uint32_t len;
  std::memcpy(&len, head, 4);
  len = ntohl(len);
  uint16_t nid;
  std::memcpy(&nid, head + 4, 2);
  *id = ntohs(nid);
  body->resize(len - 2);
  return len == 2 || recvN(fd, body->data(), len - 2);
}

struct Me {
  uint64_t pid = 0;
  std::string token;
  float x = 0, y = 0;
  int hp = 100;
};

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <port> <account> [--drop-after-ms N]\n",
                 argv[0]);
    return 1;
  }
  uint16_t port = static_cast<uint16_t>(std::atoi(argv[1]));
  std::string account = argv[2];
  int64_t dropAfterMs = -1;
  if (argc >= 5 && std::string(argv[3]) == "--drop-after-ms") {
    dropAfterMs = std::atoll(argv[4]);
  }

  int fd = connectTo(port);
  Me me;
  {  // 登录
    pb::LoginReq req;
    req.set_account(account);
    sendMsg(fd, MsgId::kLoginReq, req);
  }

  bool inRoom = false;
  bool dropped = false;
  uint64_t enterMs = 0, lastInputMs = 0, lastHbMs = 0;
  uint32_t seq = 0;
  pb::StateSnapshot world;  // 最近一次看到的战场(按 player_id 合并增量)

  auto mergeSnapshot = [&](const pb::StateSnapshot& s) {
    if (s.full()) {
      world = s;
      return;
    }
    for (const auto& p : s.players()) {
      bool found = false;
      for (auto& w : *world.mutable_players()) {
        if (w.player_id() == p.player_id()) {
          w = p;
          found = true;
        }
      }
      if (!found) *world.add_players() = p;
    }
  };

  while (true) {
    uint16_t id;
    std::string body;
    if (recvFrame(fd, &id, &body)) {
      switch (id) {
        case MsgId::kLoginResp: {
          pb::LoginResp resp;
          resp.ParseFromString(body);
          if (resp.code() != 0) {
            std::printf("[%s] login/reconnect rejected\n", account.c_str());
            return 1;
          }
          if (me.pid == 0) {  // 首次登录:存 token,发起匹配
            me.pid = resp.player_id();
            me.token = resp.session_token();
            std::printf("[%s] login ok, pid=%llu, matching...\n",
                        account.c_str(),
                        static_cast<unsigned long long>(me.pid));
            pb::MatchReq mr;
            mr.set_player_id(me.pid);
            sendMsg(fd, MsgId::kMatchReq, mr);
          } else {
            std::printf("[%s] reconnected\n", account.c_str());
          }
          break;
        }
        case MsgId::kEnterRoom: {
          pb::EnterRoom er;
          er.ParseFromString(body);
          if (er.room_id() == 0) {
            std::printf("[%s] match timeout\n", account.c_str());
            return 1;
          }
          std::printf("[%s] enter room %llu\n", account.c_str(),
                      static_cast<unsigned long long>(er.room_id()));
          inRoom = true;
          enterMs = nowMs();
          break;
        }
        case MsgId::kStateSnapshot: {
          pb::StateSnapshot snap;
          snap.ParseFromString(body);
          mergeSnapshot(snap);
          for (const auto& p : world.players()) {
            if (p.player_id() == me.pid) {
              me.x = p.x();
              me.y = p.y();
              me.hp = p.hp();
            }
          }
          break;
        }
        case MsgId::kBattleEnd: {
          pb::BattleEnd end;
          end.ParseFromString(body);
          std::printf("[%s] battle end, winner=%llu → %s\n", account.c_str(),
                      static_cast<unsigned long long>(end.winner_id()),
                      end.winner_id() == me.pid ? "WIN" : "LOSE");
          return 0;
        }
        default:
          break;
      }
    }

    uint64_t now = nowMs();

    // 演示断线重连
    if (inRoom && !dropped && dropAfterMs >= 0 &&
        now - enterMs > static_cast<uint64_t>(dropAfterMs)) {
      dropped = true;
      ::close(fd);
      std::printf("[%s] -- dropping connection, reconnect with token --\n",
                  account.c_str());
      ::usleep(500 * 1000);
      fd = connectTo(port);
      pb::ReconnectReq rc;
      rc.set_player_id(me.pid);
      rc.set_session_token(me.token);
      sendMsg(fd, MsgId::kReconnectReq, rc);
      continue;
    }

    // 心跳 2s 一次
    if (now - lastHbMs > 2000) {
      lastHbMs = now;
      pb::Heartbeat hb;
      hb.set_client_ms(static_cast<int64_t>(now));
      sendMsg(fd, MsgId::kHeartbeat, hb);
    }

    // 战斗决策 100ms 一次:朝最近敌人移动,近身开火
    if (inRoom && me.hp > 0 && now - lastInputMs > 100) {
      lastInputMs = now;
      const pb::PlayerState* enemy = nullptr;
      float best = 1e9f;
      for (const auto& p : world.players()) {
        if (p.player_id() == me.pid || p.hp() <= 0) continue;
        float d = std::hypot(p.x() - me.x, p.y() - me.y);
        if (d < best) {
          best = d;
          enemy = &p;
        }
      }
      if (enemy) {
        pb::PlayerInput in;
        in.set_seq(++seq);
        float dx = enemy->x() - me.x, dy = enemy->y() - me.y;
        float len = std::hypot(dx, dy);
        if (len > 1.f) {
          in.set_move_x(dx / len);
          in.set_move_y(dy / len);
        }
        in.set_attack(best <= 14.f);  // 略小于射程,贴脸再打
        sendMsg(fd, MsgId::kPlayerInput, in);
        std::printf("[%s] hp=%d pos=(%.0f,%.0f) enemy_dist=%.0f%s\n",
                    account.c_str(), me.hp, me.x, me.y, best,
                    best <= 14.f ? " FIRE" : "");
      }
    }
  }
}
