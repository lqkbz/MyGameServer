#pragma once
#include <cstdint>
#include <functional>

namespace gs::net {

class EventLoop;

// 一个 fd 的事件通道：保存关注事件、就绪事件与回调。
// 不拥有 fd。每个 Channel 只属于一个 EventLoop（one loop per thread 的基石）。
class Channel {
 public:
  using EventCallback = std::function<void()>;

  Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

  void setReadCallback(EventCallback cb) { readCb_ = std::move(cb); }
  void setWriteCallback(EventCallback cb) { writeCb_ = std::move(cb); }
  void setCloseCallback(EventCallback cb) { closeCb_ = std::move(cb); }
  void setErrorCallback(EventCallback cb) { errorCb_ = std::move(cb); }

  // 统一 ET：注册即带 EPOLLET
  void enableReading();
  void enableWriting();
  void disableWriting();
  void disableAll();
  bool isWriting() const;
  bool isNoneEvent() const { return events_ == 0; }

  int fd() const { return fd_; }
  uint32_t events() const { return events_; }
  void setRevents(uint32_t rev) { revents_ = rev; }
  void handleEvent();  // 按 revents_ 分发到各回调

  // 供 EPollPoller 记录状态：kNew/kAdded/kDeleted
  int index() const { return index_; }
  void setIndex(int i) { index_ = i; }
  EventLoop* ownerLoop() const { return loop_; }

 private:
  void update();

  EventLoop* loop_;
  const int fd_;
  uint32_t events_ = 0;
  uint32_t revents_ = 0;
  int index_ = -1;  // kNew
  EventCallback readCb_, writeCb_, closeCb_, errorCb_;
};

}  // namespace gs::net
