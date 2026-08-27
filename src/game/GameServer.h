#pragma once
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "game/MatchQueue.h"
#include "game/Room.h"
#include "game/Session.h"
#include "net/EventLoopThread.h"
#include "net/EventLoopThreadPool.h"
#include "net/TcpServer.h"
#include "proto/Dispatcher.h"
#include "proto/ProtobufCodec.h"
#include "storage/AsyncWriter.h"
#include "storage/MySqlStore.h"
#include "storage/RedisClient.h"
#include "timer/PeriodicTimer.h"
#include "timer/TimerWheel.h"

namespace gs::game {

// 存储配置。enabled=false 时服务器纯内存运行(游戏 e2e 用)
struct StorageOptions {
  bool enabled = false;
  std::string redisHost = "127.0.0.1";
  int redisPort = 6379;
  std::string mysqlHost = "127.0.0.1";
  std::string mysqlUser = "gs";
  std::string mysqlPass = "gs123";
  std::string mysqlDb = "gs";
  std::string leaderboardKey = "leaderboard";  // 测试可换 key 隔离
};

// 游戏服务器组装件。线程职责:
//   IO 线程(TcpServer 池):收发+编解码,handler 里立刻 hop 到 lobby
//   lobby 线程(单线程):会话/匹配/路由/时间轮,全部状态无锁
//   game 线程池:每个 Room 固定绑定其一,房间逻辑无锁
class GameServer {
 public:
  static constexpr uint64_t kHeartbeatTimeoutMs = 10000;  // 心跳超时踢人
  static constexpr uint64_t kMatchTimeoutMs = 30000;      // 匹配超时
  static constexpr uint64_t kSessionKeepMs = 30000;       // 断线 session 保留
  static constexpr uint64_t kSweepIntervalMs = 5000;      // 心跳巡检周期

  GameServer(net::EventLoop* baseLoop, uint16_t port, int ioThreads,
             int gameThreads, StorageOptions storeOpts = {});
  ~GameServer();  // 在正确线程上停表/清房/关 game 线程池
  void start();

 private:
  // —— 以下全部仅在 lobby 线程执行 ——
  void onLogin(const net::TcpConnectionPtr& conn, const pb::LoginReq& req);
  void onReconnect(const net::TcpConnectionPtr& conn,
                   const pb::ReconnectReq& req);
  void onMatch(const net::TcpConnectionPtr& conn, const pb::MatchReq& req);
  void onInput(const net::TcpConnectionPtr& conn, const pb::PlayerInput& in);
  void onHeartbeat(const net::TcpConnectionPtr& conn, const pb::Heartbeat& hb);
  void onDisconnect(const std::string& connName);
  void tryMatch();
  void onRoomEnd(uint64_t roomId, uint64_t winnerId);
  void expireSession(uint64_t playerId);  // 断线保留到期
  void sweepHeartbeats();
  void scheduleSweep();

  void onLeaderboard(const net::TcpConnectionPtr& conn,
                     const pb::LeaderboardReq& req);  // 任意线程,直接入存储队列
  SessionPtr sessionOf(const net::TcpConnectionPtr& conn);
  static uint64_t nowMs();
  std::string genToken();

  int gameThreads_;
  net::TcpServer server_;
  net::EventLoopThread lobbyThread_;
  net::EventLoop* lobby_ = nullptr;
  std::unique_ptr<net::EventLoopThreadPool> gamePool_;
  proto::Dispatcher dispatcher_;
  proto::ProtobufCodec codec_;

  // lobby 线程私有状态
  std::unordered_map<std::string, uint64_t> connToPlayer_;
  std::unordered_map<uint64_t, SessionPtr> sessions_;
  std::unordered_map<uint64_t, RoomPtr> rooms_;
  MatchQueue matchQueue_;
  std::unordered_map<uint64_t, uint64_t> matchTimer_;  // playerId → timerId
  timer::TimerWheel wheel_;
  std::unique_ptr<timer::PeriodicTimer> wheelDriver_;
  uint64_t nextPlayerId_ = 1;
  uint64_t nextRoomId_ = 1;
  std::mt19937_64 rng_{std::random_device{}()};

  // 存储:客户端对象仅存储线程触碰;writer_ 声明在最后 →
  // 析构最先发生,flush 剩余写任务时 redis_/mysql_ 仍然活着
  StorageOptions storeOpts_;
  std::atomic<bool> storageReady_{false};
  storage::RedisClient redis_;
  storage::MySqlStore mysql_;
  std::unique_ptr<storage::AsyncWriter> writer_;
};

}  // namespace gs::game
