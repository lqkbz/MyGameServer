// 用法: ./echo_server [port] [io线程数]
#include <cstdio>
#include <cstdlib>

#include "net/EventLoop.h"
#include "net/InetAddress.h"
#include "net/TcpConnection.h"
#include "net/TcpServer.h"
using namespace gs::net;

int main(int argc, char* argv[]) {
  uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 9000;
  int threads = argc > 2 ? std::atoi(argv[2]) : 4;

  EventLoop loop;  // 主 Reactor：只 accept
  TcpServer server(&loop, InetAddress(port), "echo");
  server.setThreadNum(threads);
  server.setConnectionCallback([](const TcpConnectionPtr& conn) {
    std::printf("%s %s\n", conn->peerAddress().toIpPort().c_str(),
                conn->connected() ? "UP" : "DOWN");
  });
  server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) {
    conn->send(buf->retrieveAllAsString());
  });
  server.start();
  std::printf("echo_server listening on %u, %d io threads\n", port, threads);
  loop.loop();
}
