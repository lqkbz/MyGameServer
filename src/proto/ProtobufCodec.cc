#include "proto/ProtobufCodec.h"

#include <arpa/inet.h>

#include <cstring>

#include "net/Buffer.h"
#include "net/TcpConnection.h"

namespace gs::proto {

void ProtobufCodec::onMessage(const net::TcpConnectionPtr& conn,
                              net::Buffer* buf) {
  while (buf->readableBytes() >= kHeaderLen) {
    uint32_t netLen;
    std::memcpy(&netLen, buf->peek(), sizeof netLen);
    const uint32_t len = ntohl(netLen);
    if (len < kIdLen || len > kMaxFrameLen) {  // 非法帧：流已不可信，断连自保
      if (conn) conn->shutdown();
      buf->retrieveAll();
      return;
    }
    if (buf->readableBytes() < kHeaderLen + len) break;  // 半包，等下次数据
    uint16_t netId;
    std::memcpy(&netId, buf->peek() + kHeaderLen, sizeof netId);
    const uint16_t msgId = ntohs(netId);
    cb_(conn, msgId, buf->peek() + kHeaderLen + kIdLen, len - kIdLen);
    buf->retrieve(kHeaderLen + len);
  }
}

std::string ProtobufCodec::encode(uint16_t msgId,
                                  const google::protobuf::Message& msg) {
  const size_t bodyLen = msg.ByteSizeLong();
  std::string frame(kHeaderLen + kIdLen + bodyLen, '\0');
  const uint32_t netLen = htonl(static_cast<uint32_t>(kIdLen + bodyLen));
  const uint16_t netId = htons(msgId);
  std::memcpy(&frame[0], &netLen, sizeof netLen);
  std::memcpy(&frame[kHeaderLen], &netId, sizeof netId);
  msg.SerializeToArray(&frame[kHeaderLen + kIdLen], static_cast<int>(bodyLen));
  return frame;
}

void ProtobufCodec::send(const net::TcpConnectionPtr& conn, uint16_t msgId,
                         const google::protobuf::Message& msg) {
  if (conn) conn->send(encode(msgId, msg));
}

}  // namespace gs::proto
