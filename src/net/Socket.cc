#include "net/Socket.h"

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

namespace gs::net {

int createNonblockingSocket() {
  int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    std::perror("socket");
    std::abort();
  }
  return fd;
}

Socket::~Socket() { ::close(fd_); }

void Socket::bindAddress(const InetAddress& addr) {
  if (::bind(fd_, addr.sockAddr(), sizeof(sockaddr_in)) < 0) {
    std::perror("bind");
    std::abort();
  }
}

void Socket::listen() {
  if (::listen(fd_, SOMAXCONN) < 0) {
    std::perror("listen");
    std::abort();
  }
}

int Socket::accept(InetAddress* peeraddr) {
  socklen_t len = sizeof(sockaddr_in);
  int connfd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&peeraddr->raw()),
                         &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  return connfd;  // 可能为 -1（EAGAIN 等），调用方判断
}

void Socket::setReuseAddr(bool on) {
  int opt = on ? 1 : 0;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
}

void Socket::setNoDelay(bool on) {
  int opt = on ? 1 : 0;
  ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt);
}

}  // namespace gs::net
