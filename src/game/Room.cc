#include "game/Room.h"

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "proto/MsgId.h"

namespace gs::game {

Room::Room(net::EventLoop* loop, uint64_t id,
           std::vector<std::pair<uint64_t, net::TcpConnectionPtr>> players,
           proto::ProtobufCodec* codec, EndCallback onEnd)
    : loop_(loop), id_(id), codec_(codec), onEnd_(std::move(onEnd)) {
  // 出生点:左右对峙
  float xs[2] = {30.f, 70.f};
  int i = 0;
  for (auto& [pid, conn] : players) {
    sim_.addPlayer(pid, xs[i % 2], 50.f);
    conns_[pid] = conn;
    ++i;
  }
}

void Room::start() {
  // 进房通知:带房间号与全部成员
  pb::EnterRoom enter;
  enter.set_room_id(id_);
  for (auto pid : sim_.playerIds()) enter.add_player_ids(pid);
  broadcast(proto::kEnterRoom, enter);
  for (auto& [pid, conn] : conns_) sendFullSnapshot(conn);
  ticker_ = std::make_unique<timer::PeriodicTimer>(loop_, kTickMs,
                                                   [this] { onTick(); });
}

void Room::onInput(uint64_t playerId, const pb::PlayerInput& in) {
  if (ended_) return;
  sim_.applyInput(playerId, in);
}

void Room::rebind(uint64_t playerId, const net::TcpConnectionPtr& conn) {
  auto it = conns_.find(playerId);
  if (it == conns_.end()) return;
  it->second = conn;
  // 重连方需要完整上下文:先补 EnterRoom 再补全量快照
  pb::EnterRoom enter;
  enter.set_room_id(id_);
  for (auto pid : sim_.playerIds()) enter.add_player_ids(pid);
  codec_->send(conn, proto::kEnterRoom, enter);
  sendFullSnapshot(conn);
}

void Room::dropConn(uint64_t playerId) {
  auto it = conns_.find(playerId);
  if (it != conns_.end()) it->second.reset();  // 战斗继续,快照不再发它
}

void Room::forceEnd(uint64_t winnerId) {
  if (!ended_) endBattle(winnerId);
}

void Room::stop() {
  ended_ = true;
  ticker_.reset();  // 仅允许从"非 ticker 回调栈"的 loop 任务里调用
}

void Room::onTick() {
  if (ended_) return;
  ++tick_;
  auto changed = sim_.step(kTickMs);
  auto projectiles = sim_.projectileIds();
  auto removedProjectiles = sim_.takeRemovedProjectileIds();
  if (!changed.empty() || !projectiles.empty() || !removedProjectiles.empty()) {
    pb::StateSnapshot snap;
    snap.set_tick(tick_);
    snap.set_full(false);  // 增量:只发本 tick 有变化的实体
    for (auto pid : changed) sim_.fillState(pid, snap.add_players());
    for (auto projectileId : projectiles)
      sim_.fillProjectileState(projectileId, snap.add_projectiles());
    for (auto projectileId : removedProjectiles)
      snap.add_removed_projectile_ids(projectileId);
    broadcast(proto::kStateSnapshot, snap);
  }
  if (uint64_t w = sim_.winnerOrZero(); w != 0) endBattle(w);
}

void Room::endBattle(uint64_t winnerId) {
  ended_ = true;
  pb::BattleEnd end;
  end.set_winner_id(winnerId);
  broadcast(proto::kBattleEnd, end);
  // 不能在 ticker 自己的回调栈里析构 ticker(Channel 正在 handleEvent),
  // 投递到下一轮事件循环再停;shared_from_this 保证任务执行前房间活着
  loop_->queueInLoop([self = shared_from_this()] { self->ticker_.reset(); });
  if (onEnd_) onEnd_(id_, winnerId);
}

void Room::broadcast(uint16_t msgId, const google::protobuf::Message& msg) {
  // perf(M5) 发现:逐连接 codec_->send 会对同一消息重复序列化。
  // 广播语义下帧内容完全相同 → 编码一次,各连接只拷贝字节
  std::string frame = proto::ProtobufCodec::encode(msgId, msg);
  for (auto& [pid, conn] : conns_) {
    if (conn) conn->send(frame);
  }
}

void Room::sendFullSnapshot(const net::TcpConnectionPtr& conn) {
  if (!conn) return;
  pb::StateSnapshot snap;
  snap.set_tick(tick_);
  snap.set_full(true);
  for (auto pid : sim_.playerIds()) sim_.fillState(pid, snap.add_players());
  for (auto projectileId : sim_.projectileIds())
    sim_.fillProjectileState(projectileId, snap.add_projectiles());
  codec_->send(conn, proto::kStateSnapshot, snap);
}

}  // namespace gs::game
