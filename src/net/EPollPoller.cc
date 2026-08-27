#include "net/EPollPoller.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include "net/Channel.h"

namespace gs::net {

namespace {
constexpr int kNew = -1;     // 从未加入 epoll
constexpr int kAdded = 1;    // 已在 epoll 中
constexpr int kDeleted = 2;  // 曾加入后被 DEL，仍在 channels_ 表里
}  // namespace

EPollPoller::EPollPoller()
    : epfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitEventListSize) {
  if (epfd_ < 0) {
    std::perror("epoll_create1");
    std::abort();
  }
}

EPollPoller::~EPollPoller() { ::close(epfd_); }

void EPollPoller::poll(int timeoutMs, std::vector<Channel*>* activeChannels) {
  int n = ::epoll_wait(epfd_, events_.data(), static_cast<int>(events_.size()),
                       timeoutMs);
  if (n < 0) {
    if (errno != EINTR) std::perror("epoll_wait");
    return;
  }
  for (int i = 0; i < n; ++i) {
    auto* ch = static_cast<Channel*>(events_[i].data.ptr);
    ch->setRevents(events_[i].events);
    activeChannels->push_back(ch);
  }
  // 就绪列表满载说明并发事件多，扩容减少下轮截断
  if (static_cast<size_t>(n) == events_.size()) {
    events_.resize(events_.size() * 2);
  }
}

void EPollPoller::updateChannel(Channel* ch) {
  const int idx = ch->index();
  if (idx == kNew || idx == kDeleted) {
    channels_[ch->fd()] = ch;
    ch->setIndex(kAdded);
    update(EPOLL_CTL_ADD, ch);
  } else {  // kAdded
    if (ch->isNoneEvent()) {
      update(EPOLL_CTL_DEL, ch);
      ch->setIndex(kDeleted);
    } else {
      update(EPOLL_CTL_MOD, ch);
    }
  }
}

void EPollPoller::removeChannel(Channel* ch) {
  channels_.erase(ch->fd());
  if (ch->index() == kAdded) update(EPOLL_CTL_DEL, ch);
  ch->setIndex(kNew);
}

void EPollPoller::update(int op, Channel* ch) {
  epoll_event ev{};
  ev.events = ch->events();
  ev.data.ptr = ch;
  if (::epoll_ctl(epfd_, op, ch->fd(), &ev) < 0) std::perror("epoll_ctl");
}

}  // namespace gs::net
