#include "net/EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "net/Channel.h"
#include "net/EPollPoller.h"

namespace gs::net {

EventLoop::EventLoop()
    : threadId_(std::this_thread::get_id()),
      poller_(std::make_unique<EPollPoller>()),
      wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_)) {
  if (wakeupFd_ < 0) {
    std::perror("eventfd");
    std::abort();
  }
  wakeupChannel_->setReadCallback([this] { handleWakeup(); });
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  poller_->removeChannel(wakeupChannel_.get());
  ::close(wakeupFd_);
}

void EventLoop::loop() {
  quit_ = false;
  while (!quit_) {
    activeChannels_.clear();
    poller_->poll(10000, &activeChannels_);
    for (Channel* ch : activeChannels_) ch->handleEvent();
    doPendingFunctors();
  }
}

void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    uint64_t one = 1;
    ::write(wakeupFd_, &one, sizeof one);
  }
}

void EventLoop::runInLoop(Functor cb) {
  if (isInLoopThread()) {
    cb();
  } else {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    pendingFunctors_.push_back(std::move(cb));
  }
  // 无条件唤醒：即使是本线程（可能尚未进入 loop，或正阻塞在 poll），
  // 写一次 eventfd 保证任务不会等到 poll 超时才被执行
  uint64_t one = 1;
  ::write(wakeupFd_, &one, sizeof one);
}

void EventLoop::handleWakeup() {
  uint64_t v;
  ::read(wakeupFd_, &v, sizeof v);
}

void EventLoop::doPendingFunctors() {
  // swap 出来再执行：缩短临界区，且回调里可以安全地再 queueInLoop
  std::vector<Functor> functors;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    functors.swap(pendingFunctors_);
  }
  for (auto& f : functors) f();
}

void EventLoop::updateChannel(Channel* ch) { poller_->updateChannel(ch); }
void EventLoop::removeChannel(Channel* ch) { poller_->removeChannel(ch); }

}  // namespace gs::net
