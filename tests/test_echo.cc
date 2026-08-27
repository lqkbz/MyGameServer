#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "net/TcpServer.h"
using namespace gs::net;

namespace {
// 阻塞客户端：连上、发一条、收回显、比对
std::string echoOnce(uint16_t port, const std::string& msg) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
    ::close(fd);
    return "";
  }
  ::send(fd, msg.data(), msg.size(), 0);
  std::string got;
  char buf[4096];
  while (got.size() < msg.size()) {
    ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0) break;
    got.append(buf, n);
  }
  ::close(fd);
  return got;
}
}  // namespace

// 多客户端并发 echo：覆盖 多IO线程 + ET读写 + 跨线程send 整条链路。
// server 在主线程栈上、baseLoop 驱动，生命周期严格短于 loop —— 无退出期竞态。
TEST(Echo, MultiClient) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(15502, "127.0.0.1"), "echo");
  server.setThreadNum(2);
  server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) {
    conn->send(buf->retrieveAllAsString());
  });
  server.start();

  std::atomic<int> ok{0};
  std::thread driver([&] {
    std::vector<std::thread> clients;
    for (int i = 0; i < 8; ++i) {
      clients.emplace_back([i, &ok] {
        std::string msg =
            "hello-" + std::to_string(i) + "-" + std::string(3000, 'x');
        if (echoOnce(15502, msg) == msg) ++ok;
      });
    }
    for (auto& t : clients) t.join();
    loop.quit();
  });
  loop.loop();
  driver.join();
  EXPECT_EQ(ok.load(), 8);
}

// 对端"发完就关"：数据与 FIN 同一轮事件到达，消息不能丢
TEST(Echo, SendThenImmediateClose) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(15503, "127.0.0.1"), "sink");
  std::string received;
  server.setMessageCallback([&](const TcpConnectionPtr&, Buffer* buf) {
    received += buf->retrieveAllAsString();
  });
  server.start();

  std::thread driver([&] {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(15503);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::send(fd, "last-words", 10, 0);
    ::close(fd);  // 发完立即关
    // 给服务端一点时间处理事件
    ::usleep(200 * 1000);
    loop.quit();
  });
  loop.loop();
  driver.join();
  EXPECT_EQ(received, "last-words");
}
