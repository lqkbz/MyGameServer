#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "net/Channel.h"
#include "net/EventLoop.h"
using namespace gs::net;

// eventfd 自触发：验证 poll → Channel 分发 → 回调 → quit 整条链路
TEST(EventLoop, DispatchAndQuit) {
  EventLoop loop;
  int efd = ::eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);  // 初值 1，立即可读
  Channel ch(&loop, efd);
  int fired = 0;
  ch.setReadCallback([&] {
    uint64_t v;
    ::read(efd, &v, sizeof v);
    ++fired;
    loop.quit();
  });
  ch.enableReading();
  loop.loop();  // 应触发一次回调后退出
  EXPECT_EQ(fired, 1);
  ch.disableAll();
  loop.removeChannel(&ch);
  ::close(efd);
}

// 同线程 runInLoop 直接执行
TEST(EventLoop, RunInLoopSameThread) {
  EventLoop loop;
  int called = 0;
  loop.runInLoop([&] { ++called; });
  EXPECT_EQ(called, 1);
}

// queueInLoop 的任务在 loop() 内被执行
TEST(EventLoop, QueueInLoop) {
  EventLoop loop;
  int called = 0;
  loop.queueInLoop([&] {
    ++called;
    loop.quit();
  });
  loop.loop();
  EXPECT_EQ(called, 1);
}
