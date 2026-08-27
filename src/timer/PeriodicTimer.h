#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#include "net/Channel.h"

namespace gs::net {
class EventLoop;
}

namespace gs::timer {

// timerfd 周期定时器:内核按 interval 触发可读事件,走统一的
// epoll/Channel 事件分发——定时也是"事件",与 IO 同一套模型,
// 不需要单独的定时线程。构造即启动,析构停止。
// 必须在所属 loop 线程构造/析构。
class PeriodicTimer {
 public:
  PeriodicTimer(net::EventLoop* loop, uint64_t intervalMs,
                std::function<void()> cb);
  ~PeriodicTimer();
  PeriodicTimer(const PeriodicTimer&) = delete;
  PeriodicTimer& operator=(const PeriodicTimer&) = delete;

 private:
  void handleRead();  // 读掉超时次数再回调

  net::EventLoop* loop_;
  int timerfd_;
  std::unique_ptr<net::Channel> channel_;
  std::function<void()> cb_;
};

}  // namespace gs::timer
