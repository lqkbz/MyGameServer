#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "game/BattleSim.h"
#include "net/Callbacks.h"
#include "proto/ProtobufCodec.h"
#include "timer/PeriodicTimer.h"

namespace gs::net {
class EventLoop;
}

namespace gs::game {

// 战斗房间:固定绑定一个 game loop,所有成员函数都必须在该 loop
// 线程执行(外部用 runInLoop 投递)——单线程模型,房间内零锁。
// 20Hz tick:收集输入(状态式,存在 sim 里)→ step 模拟玩家/子弹
// → 广播玩家变化、存活子弹与子弹销毁 tombstone。
class Room : public std::enable_shared_from_this<Room> {
 public:
  static constexpr uint32_t kTickMs = 50;  // 20Hz

  // onEnd(roomId, winnerId) 在 room loop 线程回调,由 GameServer hop 回 lobby
  using EndCallback = std::function<void(uint64_t, uint64_t)>;

  Room(net::EventLoop* loop, uint64_t id,
       std::vector<std::pair<uint64_t, net::TcpConnectionPtr>> players,
       proto::ProtobufCodec* codec, EndCallback onEnd);

  // 以下全部要求在 loop_ 线程调用
  void start();  // 发 EnterRoom + 全量快照,启动 tick
  void onInput(uint64_t playerId, const pb::PlayerInput& in);
  void rebind(uint64_t playerId, const net::TcpConnectionPtr& conn);  // 重连
  void dropConn(uint64_t playerId);  // 断线:解绑连接,战斗继续
  void forceEnd(uint64_t winnerId);  // 对手 session 过期等强制结算
  void stop();                       // 服务器关停:停 tick,不再广播

  uint64_t id() const { return id_; }
  net::EventLoop* loop() const { return loop_; }

 private:
  void onTick();
  void broadcast(uint16_t msgId, const google::protobuf::Message& msg);
  void sendFullSnapshot(const net::TcpConnectionPtr& conn);
  void endBattle(uint64_t winnerId);

  net::EventLoop* loop_;
  const uint64_t id_;
  BattleSim sim_;
  std::unordered_map<uint64_t, net::TcpConnectionPtr> conns_;
  proto::ProtobufCodec* codec_;
  EndCallback onEnd_;
  std::unique_ptr<timer::PeriodicTimer> ticker_;
  uint32_t tick_ = 0;
  bool ended_ = false;
};

using RoomPtr = std::shared_ptr<Room>;

}  // namespace gs::game
