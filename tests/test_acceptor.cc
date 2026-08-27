#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include "net/Acceptor.h"
#include "net/EventLoop.h"
#include "net/InetAddress.h"
using namespace gs::net;

// 真实 TCP 连接触发 newConnectionCallback
TEST(Acceptor, AcceptsConnection) {
  EventLoop loop;
  InetAddress listenAddr(15501, "127.0.0.1");
  Acceptor acceptor(&loop, listenAddr);
  int accepted = 0;
  acceptor.setNewConnectionCallback([&](int connfd, const InetAddress& peer) {
    ++accepted;
    EXPECT_GT(connfd, 0);
    EXPECT_NE(peer.toIpPort().find("127.0.0.1"), std::string::npos);
    ::close(connfd);
    loop.quit();
  });
  acceptor.listen();

  std::thread client([] {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(15501);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::close(fd);
  });
  loop.loop();
  client.join();
  EXPECT_EQ(accepted, 1);
}
