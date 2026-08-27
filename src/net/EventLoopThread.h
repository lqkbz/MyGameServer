#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>

namespace gs::net {

class EventLoop;

// IO 线程：线程函数内栈上创建 EventLoop（保证 one loop per thread），
// startLoop 用条件变量等 loop 构造完成后返回指针
class EventLoopThread {
 public:
  EventLoopThread() = default;
  ~EventLoopThread();
  EventLoopThread(const EventLoopThread&) = delete;
  EventLoopThread& operator=(const EventLoopThread&) = delete;

  EventLoop* startLoop();

 private:
  void threadFunc();

  EventLoop* loop_ = nullptr;  // guarded by mutex_
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
};

}  // namespace gs::net
