#include "net/Acceptor.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "net/EventLoop.h"

namespace gs::net {

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr)
    : loop_(loop),
      acceptSocket_(createNonblockingSocket()),
      acceptChannel_(loop, acceptSocket_.fd()) {
  acceptSocket_.setReuseAddr(true);
  acceptSocket_.bindAddress(listenAddr);
  acceptChannel_.setReadCallback([this] { handleRead(); });
}

Acceptor::~Acceptor() {
  acceptChannel_.disableAll();
  loop_->removeChannel(&acceptChannel_);
}

void Acceptor::listen() {
  acceptSocket_.listen();
  acceptChannel_.enableReading();
}

void Acceptor::handleRead() {
  // ET 模式：一次事件可能对应多个排队连接，必须循环 accept 到 EAGAIN
  while (true) {
    InetAddress peer(0);
    int connfd = acceptSocket_.accept(&peer);
    if (connfd >= 0) {
      if (cb_) {
        cb_(connfd, peer);
      } else {
        ::close(connfd);
      }
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      if (errno == EMFILE) {  // fd 耗尽：打日志退出本轮，避免 busy loop
        std::perror("accept EMFILE");
        break;
      }
      std::perror("accept");
      break;
    }
  }
}

}  // namespace gs::net
