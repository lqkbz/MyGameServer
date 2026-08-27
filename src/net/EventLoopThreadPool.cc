#include "net/EventLoopThreadPool.h"

#include "net/EventLoopThread.h"

namespace gs::net {

EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::start(int numThreads) {
  for (int i = 0; i < numThreads; ++i) {
    auto t = std::make_unique<EventLoopThread>();
    loops_.push_back(t->startLoop());
    threads_.push_back(std::move(t));
  }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
  if (loops_.empty()) return baseLoop_;
  EventLoop* loop = loops_[next_];
  next_ = (next_ + 1) % loops_.size();
  return loop;
}

}  // namespace gs::net
