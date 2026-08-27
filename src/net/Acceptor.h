#pragma once
#include <functional>

#include "net/Channel.h"
#include "net/InetAddress.h"
#include "net/Socket.h"

namespace gs::net {

class EventLoop;

// 监听套接字封装：只在主 Reactor（baseLoop）里工作，只管 accept，
// 拿到 connfd 后交给上层（TcpServer）分发给 IO 线程
class Acceptor {
 public:
  using NewConnectionCallback =
      std::function<void(int connfd, const InetAddress& peer)>;

  Acceptor(EventLoop* loop, const InetAddress& listenAddr);
  ~Acceptor();
  Acceptor(const Acceptor&) = delete;
  Acceptor& operator=(const Acceptor&) = delete;

  void setNewConnectionCallback(NewConnectionCallback cb) {
    cb_ = std::move(cb);
  }
  void listen();

 private:
  void handleRead();  // ET：循环 accept 到 EAGAIN

  EventLoop* loop_;
  Socket acceptSocket_;
  Channel acceptChannel_;
  NewConnectionCallback cb_;
};

}  // namespace gs::net
