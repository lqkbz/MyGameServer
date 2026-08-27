#include <gtest/gtest.h>

#include "game.pb.h"
#include "game/BattleSim.h"
using gs::game::BattleSim;
using gs::pb::PlayerInput;

namespace {
PlayerInput makeInput(float mx, float my, bool atk, uint32_t seq = 1) {
  PlayerInput in;
  in.set_move_x(mx);
  in.set_move_y(my);
  in.set_attack(atk);
  in.set_seq(seq);
  return in;
}
}  // namespace

TEST(BattleSim, MoveAndClamp) {
  BattleSim sim;
  sim.addPlayer(1, 50, 50);
  sim.applyInput(1, makeInput(1, 0, false));
  auto changed = sim.step(1000);  // 1s → 右移 kSpeed=20
  ASSERT_EQ(changed.size(), 1u);
  gs::pb::PlayerState st;
  sim.fillState(1, &st);
  EXPECT_FLOAT_EQ(st.x(), 70);
  EXPECT_FLOAT_EQ(st.y(), 50);
  // 持续右移撞墙 clamp 到 100
  for (int i = 0; i < 5; ++i) sim.step(1000);
  sim.fillState(1, &st);
  EXPECT_FLOAT_EQ(st.x(), 100);
}

TEST(BattleSim, NoInputNoChange) {
  BattleSim sim;
  sim.addPlayer(1, 50, 50);
  sim.addPlayer(2, 60, 50);
  EXPECT_TRUE(sim.step(50).empty());
}

TEST(BattleSim, ProjectileDamageIsNotInstant) {
  BattleSim sim;
  sim.addPlayer(1, 0, 0);
  sim.addPlayer(2, 50, 50);
  sim.applyInput(1, makeInput(0, 0, true));
  sim.step(50);  // 只生成子弹；伤害必须等权威弹道命中后结算
  gs::pb::PlayerState st;
  sim.fillState(2, &st);
  EXPECT_EQ(st.hp(), 100);
  ASSERT_EQ(sim.projectileIds().size(), 1u);
  gs::pb::ProjectileState projectile;
  sim.fillProjectileState(sim.projectileIds()[0], &projectile);
  EXPECT_EQ(projectile.owner_id(), 1u);
  EXPECT_GT(projectile.velocity_x(), 0.f);
  EXPECT_GT(projectile.velocity_y(), 0.f);
}

TEST(BattleSim, ProjectileTravelsHitsAndRespectsCooldown) {
  BattleSim sim;
  sim.addPlayer(1, 50, 50);
  sim.addPlayer(2, 60, 50);
  sim.applyInput(1, makeInput(0, 0, true, 1));
  sim.step(50);  // tick1:枪口生成
  gs::pb::PlayerState st;
  sim.fillState(2, &st);
  EXPECT_EQ(st.hp(), 100);
  ASSERT_EQ(sim.projectileIds().size(), 1u);
  const uint64_t firstProjectile = sim.projectileIds()[0];

  sim.applyInput(1, makeInput(0, 0, false, 2));
  sim.step(50);  // tick2:飞行
  sim.step(50);  // tick3:线段碰撞命中
  sim.fillState(2, &st);
  EXPECT_EQ(st.hp(), 75);
  EXPECT_TRUE(sim.projectileIds().empty());
  auto removed = sim.takeRemovedProjectileIds();
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], firstProjectile);

  // CD 尚未结束，不生成第二颗。
  sim.applyInput(1, makeInput(0, 0, true, 2));
  sim.step(50);
  EXPECT_TRUE(sim.projectileIds().empty());
  sim.fillState(2, &st);
  EXPECT_EQ(st.hp(), 75);
  sim.applyInput(1, makeInput(0, 0, false, 3));
  sim.step(400);  // CD 走完
  sim.applyInput(1, makeInput(0, 0, true, 4));
  sim.step(50);
  EXPECT_EQ(sim.projectileIds().size(), 1u);
  sim.applyInput(1, makeInput(0, 0, false, 5));
  sim.step(50);
  sim.step(50);
  sim.fillState(2, &st);
  EXPECT_EQ(st.hp(), 50);
}

TEST(BattleSim, KillYieldsWinner) {
  BattleSim sim;
  sim.addPlayer(1, 50, 50);
  sim.addPlayer(2, 60, 50);
  EXPECT_EQ(sim.winnerOrZero(), 0u);
  for (int i = 0; i < 4; ++i) {  // 4 发 × 25 = 100
    sim.applyInput(1, makeInput(0, 0, true, i * 2 + 1));
    sim.step(50);  // 生成
    sim.applyInput(1, makeInput(0, 0, false, i * 2 + 2));
    sim.step(50);  // 飞行
    sim.step(50);  // 命中
    sim.step(500); // 等 CD
  }
  gs::pb::PlayerState st;
  sim.fillState(2, &st);
  EXPECT_EQ(st.hp(), 0);
  EXPECT_EQ(sim.winnerOrZero(), 1u);
  // 死人不能再动/攻击
  sim.applyInput(2, makeInput(1, 0, true));
  EXPECT_TRUE(sim.step(50).empty());
}
