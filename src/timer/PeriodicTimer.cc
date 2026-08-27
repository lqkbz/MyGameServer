#include "timer/PeriodicTimer.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/EventLoop.h"

namespace gs::timer {

PeriodicTimer::PeriodicTimer(net::EventLoop* loop, uint64_t intervalMs,
                             std::function<void()> cb)
    : loop_(loop),
      timerfd_(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      channel_(std::make_unique<net::Channel>(loop, timerfd_)),
      cb_(std::move(cb)) {
  if (timerfd_ < 0) {
    std::perror("timerfd_create");
    std::abort();
  }
  itimerspec spec{};
  spec.it_interval.tv_sec = intervalMs / 1000;
  spec.it_interval.tv_nsec = (intervalMs % 1000) * 1000000;
  spec.it_value = spec.it_interval;  // 首次触发也等一个周期
  ::timerfd_settime(timerfd_, 0, &spec, nullptr);
  channel_->setReadCallback([this] { handleRead(); });
  channel_->enableReading();
}

PeriodicTimer::~PeriodicTimer() {
  channel_->disableAll();
  loop_->removeChannel(channel_.get());
  ::close(timerfd_);
}

void PeriodicTimer::handleRead() {
  uint64_t expirations;  // 距上次 read 过了几个周期(loop 卡顿时 >1)
  ::read(timerfd_, &expirations, sizeof expirations);
  if (cb_) cb_();
}

}  // namespace gs::timer
