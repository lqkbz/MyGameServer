#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>

#include "net/EventLoop.h"
#include "net/EventLoopThread.h"
#include "timer/PeriodicTimer.h"
using namespace gs;

// 30ms 周期定时器触发 3 次:验证周期回调与真实时间下限。
// 生命周期编排要点:timer 的创建/销毁都以任务投递到 loop 线程执行;
// 销毁与 quit 放同一个任务,保证之后再没人碰这个栈上的 loop 对象。
TEST(PeriodicTimer, FiresPeriodically) {
  net::EventLoopThread t;
  net::EventLoop* loop = t.startLoop();
  std::atomic<int> fired{0};
  std::promise<void> enough;
  auto start = std::chrono::steady_clock::now();
  std::unique_ptr<timer::PeriodicTimer> timer;
  loop->runInLoop([&] {
    timer = std::make_unique<timer::PeriodicTimer>(loop, 30, [&] {
      if (++fired == 3) enough.set_value();  // 只通知,不在回调里自毁
    });
  });
  ASSERT_EQ(enough.get_future().wait_for(std::chrono::seconds(2)),
            std::future_status::ready)
      << "timer never fired 3 times";
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  EXPECT_GE(elapsed.count(), 90);  // 3 次 × 30ms 下限
  // 在 loop 线程销毁 timer,并用 promise 屏障确认任务真正执行完——
  // 若不等待,quit 可能抢先让 loop 退出,清理任务被跳过,
  // timer 就会在主线程析构并触碰已销毁的 EventLoop(UAF)
  std::promise<void> cleaned;
  loop->runInLoop([&] {
    timer.reset();
    cleaned.set_value();
  });
  cleaned.get_future().wait();
  // EventLoopThread 析构负责 quit + join
}
