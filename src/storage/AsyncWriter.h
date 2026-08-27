#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gs::storage {

// 异步写库队列(muduo 异步日志同款"双缓冲"模式):
// 生产者(lobby/room 线程)只把闭包 append 进前台缓冲——锁内只有一次
// push_back;存储线程醒来后整批 swap 走,在锁外慢慢执行 MySQL/Redis。
// 削峰原理:落库耗时与业务线程解耦,突发流量堆在内存缓冲里,
// 存储线程按自己的节奏消化;锁粒度 = swap 瞬间,竞争极小。
class AsyncWriter {
 public:
  using Task = std::function<void()>;

  AsyncWriter();
  ~AsyncWriter();  // flush:执行完全部已入队任务再退出
  AsyncWriter(const AsyncWriter&) = delete;
  AsyncWriter& operator=(const AsyncWriter&) = delete;

  void enqueue(Task t);  // 任意线程

 private:
  void threadFunc();

  std::mutex mutex_;
  std::condition_variable cond_;
  std::vector<Task> front_;  // 前台缓冲,guarded by mutex_
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace gs::storage
