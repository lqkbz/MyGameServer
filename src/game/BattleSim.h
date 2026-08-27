#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game.pb.h"

namespace gs::game {

// 战斗模拟(纯逻辑,无 IO/线程):Room 每 tick 调 step 推进。
// 规则:2D 战场 100×100,输入为归一化移动向量+攻击键;
// 攻击生成服务器权威子弹,由 tick 推进、碰撞和结算伤害;HP 归零判负。
// 输入是"状态式"的:applyInput 存最新输入,每 tick 持续生效,
// 直到下一条输入覆盖——丢包时沿用旧输入,天然容错。
class BattleSim {
 public:
  static constexpr float kArena = 100.f;    // 战场边长
  static constexpr float kSpeed = 20.f;     // 移速(单位/秒)
  static constexpr int kDamage = 25;        // 攻击伤害
  static constexpr float kAttackCd = 500.f; // 攻击 CD(ms)
  static constexpr int kMaxHp = 100;
  static constexpr float kProjectileSpeed = 60.f;   // 子弹速度(单位/秒)
  static constexpr float kProjectileRadius = 3.f;   // 命中半径
  static constexpr float kProjectileRange = 65.f;   // 最大飞行距离

  void addPlayer(uint64_t id, float x, float y);
  void applyInput(uint64_t id, const pb::PlayerInput& in);
  // 推进 dtMs,返回本步状态有变化(位置/血量)的玩家 id
  std::vector<uint64_t> step(uint32_t dtMs);
  uint64_t winnerOrZero() const;  // 仅剩一人存活时返回其 id,否则 0
  void fillState(uint64_t id, pb::PlayerState* out) const;
  void fillProjectileState(uint64_t id, pb::ProjectileState* out) const;
  std::vector<uint64_t> playerIds() const;
  std::vector<uint64_t> projectileIds() const;
  std::vector<uint64_t> takeRemovedProjectileIds();

 private:
  struct P {
    uint64_t id;
    float x, y;
    int hp = kMaxHp;
    uint32_t seq = 0;      // 已应用的最新输入序号
    float cdMs = 0;        // 剩余攻击 CD
    float facingX = 1.f;   // 最近一次非零移动方向;静止攻击时用于瞄准兜底
    float facingY = 0.f;
    pb::PlayerInput input; // 最新输入(状态式,持续生效)
    bool hasInput = false;
    bool alive() const { return hp > 0; }
  };

  struct Projectile {
    uint64_t id;
    uint64_t ownerId;
    float x, y;
    float vx, vy;
    float traveled = 0.f;
  };

  std::unordered_map<uint64_t, P> players_;
  std::unordered_map<uint64_t, Projectile> projectiles_;
  std::vector<uint64_t> removedProjectileIds_;
  uint64_t nextProjectileId_ = 1;
};

}  // namespace gs::game
