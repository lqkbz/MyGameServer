#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gs::net {

class Channel;
class EPollPoller;

// Reactor 核心。one loop per thread：
// 每个 IO 线程恰有一个 EventLoop，所有对 fd 的操作都在其所属 loop 线程执行，
// 跨线程只能通过 runInLoop 投递闭包 + eventfd 唤醒，从而 IO 逻辑无锁。
class EventLoop {
 public:
  using Functor = std::function<void()>;

  EventLoop();
  ~EventLoop();
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  void loop();
  void quit();  // 线程安全：非本线程调用时 wakeup

  // 在 loop 线程执行 cb：本线程直接调，否则入队并唤醒
  void runInLoop(Functor cb);
  void queueInLoop(Functor cb);

  void updateChannel(Channel* ch);
  void removeChannel(Channel* ch);
  bool isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
  }

 private:
  void handleWakeup();       // 读掉 eventfd 计数
  void doPendingFunctors();  // swap 出队列再执行

  std::atomic<bool> quit_{false};
  const std::thread::id threadId_;
  std::unique_ptr<EPollPoller> poller_;
  int wakeupFd_;
  std::unique_ptr<Channel> wakeupChannel_;
  std::vector<Channel*> activeChannels_;
  std::mutex mutex_;
  std::vector<Functor> pendingFunctors_;  // guarded by mutex_
};

}  // namespace gs::net
