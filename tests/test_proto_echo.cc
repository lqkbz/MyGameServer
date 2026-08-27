#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "game.pb.h"
#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "net/TcpServer.h"
#include "proto/Dispatcher.h"
#include "proto/MsgId.h"
#include "proto/ProtobufCodec.h"
using namespace gs;
using namespace gs::net;
using proto::Dispatcher;
using proto::MsgId;
using proto::ProtobufCodec;

namespace {
std::string recvN(int fd, size_t n) {  // 阻塞收满 n 字节
  std::string got;
  char buf[4096];
  while (got.size() < n) {
    ssize_t r = ::recv(fd, buf, sizeof buf, 0);
    if (r <= 0) break;
    got.append(buf, r);
  }
  return got;
}
}  // namespace

// 端到端：客户端分两段发一帧 EchoMsg（制造半包），服务端 Codec+Dispatcher
// 解出后原样回发，客户端解析回帧比对
TEST(ProtoEcho, EndToEnd) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(15504, "127.0.0.1"), "proto-echo");
  server.setThreadNum(2);
  Dispatcher dispatcher;
  ProtobufCodec codec([&dispatcher](const TcpConnectionPtr& c, uint16_t id,
                                    const char* d, size_t n) {
    dispatcher.onRawMessage(c, id, d, n);
  });
  dispatcher.registerHandler<pb::EchoMsg>(
      MsgId::kEcho, [&codec](const TcpConnectionPtr& c, const pb::EchoMsg& m) {
        pb::EchoMsg resp;
        resp.set_payload(m.payload());
        codec.send(c, MsgId::kEcho, resp);
      });
  server.setMessageCallback([&codec](const TcpConnectionPtr& c, Buffer* b) {
    codec.onMessage(c, b);
  });
  server.start();

  bool ok = false;
  std::thread driver([&] {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(15504);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    pb::EchoMsg m;
    m.set_payload(std::string(2000, 'p'));
    std::string frame = ProtobufCodec::encode(MsgId::kEcho, m);
    ::send(fd, frame.data(), frame.size() / 2, 0);  // 先发一半制造半包
    ::usleep(50 * 1000);
    ::send(fd, frame.data() + frame.size() / 2, frame.size() - frame.size() / 2,
           0);
    std::string back = recvN(fd, frame.size());
    pb::EchoMsg resp;
    ok = back.size() == frame.size() &&
         resp.ParseFromArray(back.data() + 6,
                             static_cast<int>(back.size()) - 6) &&
         resp.payload() == m.payload();
    ::close(fd);
    loop.quit();
  });
  loop.loop();
  driver.join();
  EXPECT_TRUE(ok);
}

// 非法帧（len 超限）→ 服务端断连
TEST(ProtoEcho, OversizeFrameShutsConnection) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(15505, "127.0.0.1"), "guard");
  ProtobufCodec codec(
      [](const TcpConnectionPtr&, uint16_t, const char*, size_t) {});
  server.setMessageCallback([&codec](const TcpConnectionPtr& c, Buffer* b) {
    codec.onMessage(c, b);
  });
  server.start();

  bool closedByServer = false;
  std::thread driver([&] {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(15505);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    uint32_t evil = htonl(10 * 1024 * 1024);  // 声称 10MB
    ::send(fd, &evil, sizeof evil, 0);
    char buf[16];
    closedByServer = ::recv(fd, buf, sizeof buf, 0) == 0;  // 收到 FIN
    ::close(fd);
    loop.quit();
  });
  loop.loop();
  driver.join();
  EXPECT_TRUE(closedByServer);
}
