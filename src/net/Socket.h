#pragma once
#include "net/InetAddress.h"

namespace gs::net {

int createNonblockingSocket();  // SOCK_NONBLOCK | SOCK_CLOEXEC，失败 abort

// listen/conn fd 的 RAII 封装，析构时 close，防 fd 泄漏
class Socket {
 public:
  explicit Socket(int fd) : fd_(fd) {}
  ~Socket();
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  int fd() const { return fd_; }
  void bindAddress(const InetAddress& addr);
  void listen();
  // accept4 直接拿到非阻塞 fd；无连接/出错返回 -1，errno 由调用方处理
  int accept(InetAddress* peeraddr);
  void setReuseAddr(bool on);
  void setNoDelay(bool on);  // 关 Nagle，游戏低延迟场景必开

 private:
  int fd_;
};

}  // namespace gs::net
