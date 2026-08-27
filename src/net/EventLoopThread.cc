#include "net/EventLoopThread.h"

#include "net/EventLoop.h"

namespace gs::net {

EventLoopThread::~EventLoopThread() {
  {
    // 必须持锁调用 quit:threadFunc 在 loop() 返回后、析构 EventLoop 前
    // 也要抢这把锁清 loop_。锁保证了 quit 执行期间 loop 对象一定活着,
    // 否则"读指针→解锁→quit"的窗口里对象可能已随线程栈销毁(UAF)。
    std::lock_guard<std::mutex> lk(mutex_);
    if (loop_) loop_->quit();
  }
  if (thread_.joinable()) thread_.join();
}

EventLoop* EventLoopThread::startLoop() {
  thread_ = std::thread([this] { threadFunc(); });
  std::unique_lock<std::mutex> lk(mutex_);
  cond_.wait(lk, [this] { return loop_ != nullptr; });
  return loop_;
}

void EventLoopThread::threadFunc() {
  EventLoop loop;  // 栈上：生命周期 = 线程生命周期
  {
    std::lock_guard<std::mutex> lk(mutex_);
    loop_ = &loop;
  }
  cond_.notify_one();
  loop.loop();
  std::lock_guard<std::mutex> lk(mutex_);
  loop_ = nullptr;
}

}  // namespace gs::net
