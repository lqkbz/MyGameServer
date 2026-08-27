#include <gtest/gtest.h>

#include "timer/TimerWheel.h"
using gs::timer::TimerWheel;

TEST(TimerWheel, FiresAtDeadlineNotBefore) {
  TimerWheel tw;
  int fired = 0;
  tw.add(300, [&] { ++fired; });
  tw.advance(200);
  EXPECT_EQ(fired, 0);  // 未到期
  tw.advance(100);
  EXPECT_EQ(fired, 1);  // 到期触发
  tw.advance(1000);
  EXPECT_EQ(fired, 1);  // 一次性,不重复
}

TEST(TimerWheel, LongDelayCrossesLevel) {  // 跨 L0 量程(6.4s)的长定时
  TimerWheel tw;
  int fired = 0;
  tw.add(10000, [&] { ++fired; });
  tw.advance(9900);
  EXPECT_EQ(fired, 0);
  tw.advance(200);
  EXPECT_EQ(fired, 1);
}

TEST(TimerWheel, CancelPreventsFiring) {
  TimerWheel tw;
  int fired = 0;
  uint64_t id = tw.add(500, [&] { ++fired; });
  tw.cancel(id);
  tw.advance(1000);
  EXPECT_EQ(fired, 0);
}

TEST(TimerWheel, BigAdvanceCatchesUp) {  // 一次大跨度 advance 内部补步进
  TimerWheel tw;
  int a = 0, b = 0;
  tw.add(100, [&] { ++a; });
  tw.add(5000, [&] { ++b; });
  tw.advance(60000);
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 1);
}

TEST(TimerWheel, MultipleTimersSameSlot) {
  TimerWheel tw;
  int fired = 0;
  for (int i = 0; i < 5; ++i) tw.add(200, [&] { ++fired; });
  tw.advance(200);
  EXPECT_EQ(fired, 5);
}

TEST(TimerWheel, ZeroDelayFiresNextTick) {  // 0 延迟归入下一步进
  TimerWheel tw;
  int fired = 0;
  tw.add(0, [&] { ++fired; });
  tw.advance(100);
  EXPECT_EQ(fired, 1);
}
