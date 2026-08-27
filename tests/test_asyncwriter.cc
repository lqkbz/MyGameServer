#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include "storage/AsyncWriter.h"
using gs::storage::AsyncWriter;

// 任务在专用存储线程执行,不在调用线程
TEST(AsyncWriter, RunsOnDedicatedThread) {
  std::thread::id workerId;
  {
    AsyncWriter w;
    std::promise<void> done;
    w.enqueue([&] {
      workerId = std::this_thread::get_id();
      done.set_value();
    });
    done.get_future().wait();
  }
  EXPECT_NE(workerId, std::this_thread::get_id());
}

// FIFO 顺序保持
TEST(AsyncWriter, PreservesOrder) {
  std::vector<int> got;
  {
    AsyncWriter w;
    for (int i = 0; i < 100; ++i) {
      w.enqueue([&got, i] { got.push_back(i); });
    }
  }  // 析构 flush
  ASSERT_EQ(got.size(), 100u);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(got[i], i);
}

// 析构必须把已入队任务全部执行完(不丢写)
TEST(AsyncWriter, DrainsOnDestroy) {
  std::atomic<int> ran{0};
  {
    AsyncWriter w;
    for (int i = 0; i < 500; ++i) w.enqueue([&] { ++ran; });
  }
  EXPECT_EQ(ran.load(), 500);
}

// 多生产者并发入队不丢任务
TEST(AsyncWriter, MultiProducer) {
  std::atomic<int> ran{0};
  {
    AsyncWriter w;
    std::vector<std::thread> ps;
    for (int t = 0; t < 4; ++t) {
      ps.emplace_back([&] {
        for (int i = 0; i < 250; ++i) w.enqueue([&] { ++ran; });
      });
    }
    for (auto& t : ps) t.join();
  }
  EXPECT_EQ(ran.load(), 1000);
}
