#include "net/Channel.h"

#include <sys/epoll.h>

#include "net/EventLoop.h"

namespace gs::net {

void Channel::enableReading() {
  events_ |= (EPOLLIN | EPOLLPRI | EPOLLET);
  update();
}

void Channel::enableWriting() {
  events_ |= EPOLLOUT;
  update();
}

void Channel::disableWriting() {
  events_ &= ~EPOLLOUT;
  update();
}

void Channel::disableAll() {
  events_ = 0;
  update();
}

bool Channel::isWriting() const { return events_ & EPOLLOUT; }

void Channel::update() { loop_->updateChannel(this); }

void Channel::handleEvent() {
  // 对端关闭且无数据可读 → close；有数据时 EPOLLIN 会带着 EPOLLRDHUP 一起来，
  // 先走读回调把剩余数据读完（读到 0 再由上层关闭）
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    if (closeCb_) closeCb_();
    return;
  }
  if (revents_ & EPOLLERR) {
    if (errorCb_) errorCb_();
  }
  if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
    if (readCb_) readCb_();
  }
  if (revents_ & EPOLLOUT) {
    if (writeCb_) writeCb_();
  }
}

}  // namespace gs::net
