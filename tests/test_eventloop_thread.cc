#include <gtest/gtest.h>

#include <future>
#include <thread>

#include "net/EventLoop.h"
#include "net/EventLoopThread.h"
#include "net/EventLoopThreadPool.h"
using namespace gs::net;

// 跨线程 runInLoop：任务必须在 loop 线程执行
TEST(EventLoopThread, CrossThreadRunInLoop) {
  EventLoopThread t;
  EventLoop* loop = t.startLoop();
  ASSERT_NE(loop, nullptr);
  std::promise<std::thread::id> p;
  loop->runInLoop([&] { p.set_value(std::this_thread::get_id()); });
  auto tid = p.get_future().get();
  EXPECT_NE(tid, std::this_thread::get_id());
}

// round-robin 分发
TEST(EventLoopThreadPool, RoundRobin) {
  EventLoop base;
  EventLoopThreadPool pool(&base);
  pool.start(2);
  EventLoop* a = pool.getNextLoop();
  EventLoop* b = pool.getNextLoop();
  EventLoop* c = pool.getNextLoop();
  EXPECT_NE(a, &base);
  EXPECT_NE(b, &base);
  EXPECT_NE(a, b);
  EXPECT_EQ(a, c);  // 轮回到第一个
}

// 0 线程退化为 baseLoop（单 Reactor 模式）
TEST(EventLoopThreadPool, ZeroThreadFallback) {
  EventLoop base;
  EventLoopThreadPool pool(&base);
  pool.start(0);
  EXPECT_EQ(pool.getNextLoop(), &base);
}
