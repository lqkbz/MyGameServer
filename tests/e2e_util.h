#pragma once
// e2e 测试公用:阻塞客户端 + 进房助手 + QuitGuard
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "game.pb.h"
#include "net/EventLoop.h"
#include "proto/MsgId.h"
#include "proto/ProtobufCodec.h"

namespace gs::test {

// 阻塞式测试客户端:同步收发帧,带超时
class TestClient {
 public:
  ~TestClient() { close(); }

  bool connect(uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    timeval tv{8, 0};  // 收包 8s 超时兜底
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    return ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
  }
  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  void send(uint16_t id, const google::protobuf::Message& m) {
    std::string f = proto::ProtobufCodec::encode(id, m);
    ::send(fd_, f.data(), f.size(), 0);
  }

  bool recvFrame(uint16_t* id, std::string* body) {
    char head[6];
    if (!recvN(head, 6)) return false;
    uint32_t len;
    std::memcpy(&len, head, 4);
    len = ntohl(len);
    uint16_t nid;
    std::memcpy(&nid, head + 4, 2);
    *id = ntohs(nid);
    body->resize(len - 2);
    return len == 2 || recvN(body->data(), len - 2);
  }

  // 跳过其他消息直到等到 wantId
  bool waitFor(uint16_t wantId, std::string* body) {
    uint16_t id;
    for (int i = 0; i < 500; ++i) {
      if (!recvFrame(&id, body)) return false;
      if (id == wantId) return true;
    }
    return false;
  }

  bool login(const std::string& account, pb::LoginResp* resp) {
    pb::LoginReq req;
    req.set_account(account);
    send(proto::kLoginReq, req);
    std::string body;
    if (!waitFor(proto::kLoginResp, &body)) return false;
    return resp->ParseFromString(body) && resp->code() == 0;
  }

 private:
  bool recvN(char* dst, size_t n) {
    size_t got = 0;
    while (got < n) {
      ssize_t r = ::recv(fd_, dst + got, n - got, 0);
      if (r <= 0) return false;
      got += r;
    }
    return true;
  }

  int fd_ = -1;
};

// 双客户端登录+匹配进房;返回房间号(0=失败)
inline uint64_t matchUp(TestClient& a, TestClient& b, const std::string& accA,
                        const std::string& accB, pb::LoginResp* ra,
                        pb::LoginResp* rb) {
  if (!a.login(accA, ra) || !b.login(accB, rb)) return 0;
  pb::MatchReq mr;
  mr.set_player_id(ra->player_id());
  a.send(proto::kMatchReq, mr);
  mr.set_player_id(rb->player_id());
  b.send(proto::kMatchReq, mr);
  std::string body;
  if (!a.waitFor(proto::kEnterRoom, &body)) return 0;
  pb::EnterRoom er;
  er.ParseFromString(body);
  if (!b.waitFor(proto::kEnterRoom, &body)) return 0;
  return er.room_id();
}

// driver 线程退出时唤醒主 loop:ASSERT 提前 return 也不会挂死测试
struct QuitGuard {
  net::EventLoop* loop;
  ~QuitGuard() { loop->quit(); }
};

}  // namespace gs::test
