#pragma once
#include <sys/epoll.h>

#include <unordered_map>
#include <vector>

namespace gs::net {

class Channel;

// epoll 封装：持有 epfd，维护 fd → Channel 映射
class EPollPoller {
 public:
  EPollPoller();
  ~EPollPoller();
  EPollPoller(const EPollPoller&) = delete;
  EPollPoller& operator=(const EPollPoller&) = delete;

  // 阻塞等事件，就绪 Channel 填入 activeChannels（已 setRevents）
  void poll(int timeoutMs, std::vector<Channel*>* activeChannels);
  void updateChannel(Channel* ch);  // ADD 或 MOD，events 为空则 DEL
  void removeChannel(Channel* ch);

 private:
  void update(int op, Channel* ch);

  static constexpr int kInitEventListSize = 16;
  int epfd_;
  std::vector<epoll_event> events_;
  std::unordered_map<int, Channel*> channels_;
};

}  // namespace gs::net
