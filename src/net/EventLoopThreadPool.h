#pragma once
#include <memory>
#include <vector>

#include "net/EventLoopThread.h"

namespace gs::net {

class EventLoop;

// IO 线程池：baseLoop（主 Reactor）只做 accept，
// 新连接 round-robin 派发给子 loop（从 Reactor）
class EventLoopThreadPool {
 public:
  explicit EventLoopThreadPool(EventLoop* baseLoop) : baseLoop_(baseLoop) {}
  ~EventLoopThreadPool();
  EventLoopThreadPool(const EventLoopThreadPool&) = delete;
  EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

  void start(int numThreads);
  EventLoop* getNextLoop();  // 仅在 baseLoop 线程调用，无需加锁

 private:
  EventLoop* baseLoop_;
  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop*> loops_;
  size_t next_ = 0;
};

}  // namespace gs::net
