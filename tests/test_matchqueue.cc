#include <gtest/gtest.h>

#include "game/MatchQueue.h"
using gs::game::MatchQueue;

TEST(MatchQueue, PairsTwoPlayers) {
  MatchQueue q;
  EXPECT_TRUE(q.push(1));
  EXPECT_FALSE(q.popPair().has_value());  // 一个人不成局
  EXPECT_TRUE(q.push(2));
  auto pair = q.popPair();
  ASSERT_TRUE(pair.has_value());
  EXPECT_EQ(pair->first, 1u);   // FIFO:先来的在前
  EXPECT_EQ(pair->second, 2u);
  EXPECT_FALSE(q.popPair().has_value());  // 队列已空
}

TEST(MatchQueue, DedupPush) {
  MatchQueue q;
  EXPECT_TRUE(q.push(1));
  EXPECT_FALSE(q.push(1));  // 重复入队拒绝
  EXPECT_FALSE(q.popPair().has_value());
}

TEST(MatchQueue, RemoveBreaksPair) {
  MatchQueue q;
  q.push(1);
  q.push(2);
  q.remove(1);  // 1 断线/超时出队
  EXPECT_FALSE(q.popPair().has_value());
  q.push(3);
  auto pair = q.popPair();
  ASSERT_TRUE(pair.has_value());
  EXPECT_EQ(pair->first, 2u);
  EXPECT_EQ(pair->second, 3u);
}
