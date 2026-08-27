#include <gtest/gtest.h>

#include <vector>

#include "game.pb.h"
#include "net/Buffer.h"
#include "proto/MsgId.h"
#include "proto/ProtobufCodec.h"
using namespace gs;
using net::Buffer;
using proto::MsgId;
using proto::ProtobufCodec;

namespace {
struct Got {
  uint16_t id;
  std::string body;
};
ProtobufCodec makeCodec(std::vector<Got>* out) {
  return ProtobufCodec([out](const net::TcpConnectionPtr&, uint16_t id,
                             const char* d, size_t n) {
    out->push_back({id, std::string(d, n)});
  });
}
}  // namespace

TEST(Codec, EncodeDecodeRoundtrip) {
  pb::EchoMsg m;
  m.set_payload("ping");
  std::string frame = ProtobufCodec::encode(MsgId::kEcho, m);
  ASSERT_EQ(frame.size(), 4 + 2 + m.ByteSizeLong());
  std::vector<Got> got;
  auto codec = makeCodec(&got);
  Buffer buf;
  buf.append(frame);
  codec.onMessage(nullptr, &buf);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].id, MsgId::kEcho);
  pb::EchoMsg back;
  ASSERT_TRUE(back.ParseFromArray(got[0].body.data(), got[0].body.size()));
  EXPECT_EQ(back.payload(), "ping");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(Codec, StickyPackets) {  // 粘包：两帧一次到达
  pb::EchoMsg a;
  a.set_payload("aaa");
  pb::Heartbeat b;
  b.set_client_ms(123);
  std::vector<Got> got;
  auto codec = makeCodec(&got);
  Buffer buf;
  buf.append(ProtobufCodec::encode(MsgId::kEcho, a));
  buf.append(ProtobufCodec::encode(MsgId::kHeartbeat, b));
  codec.onMessage(nullptr, &buf);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0].id, MsgId::kEcho);
  EXPECT_EQ(got[1].id, MsgId::kHeartbeat);
}

TEST(Codec, HalfPacket) {  // 半包：逐字节喂
  pb::EchoMsg m;
  m.set_payload("half-packet-test");
  std::string frame = ProtobufCodec::encode(MsgId::kEcho, m);
  std::vector<Got> got;
  auto codec = makeCodec(&got);
  Buffer buf;
  for (size_t i = 0; i < frame.size(); ++i) {
    buf.append(frame.data() + i, 1);
    codec.onMessage(nullptr, &buf);
    if (i + 1 < frame.size()) EXPECT_TRUE(got.empty());  // 不完整不投递
  }
  ASSERT_EQ(got.size(), 1u);
}

TEST(Codec, HalfThenSticky) {  // 半包尾 + 下一帧混合到达
  pb::EchoMsg a;
  a.set_payload(std::string(100, 'a'));
  pb::EchoMsg b;
  b.set_payload(std::string(100, 'b'));
  std::string fa = ProtobufCodec::encode(MsgId::kEcho, a);
  std::string fb = ProtobufCodec::encode(MsgId::kEcho, b);
  std::vector<Got> got;
  auto codec = makeCodec(&got);
  Buffer buf;
  buf.append(fa.substr(0, 30));
  codec.onMessage(nullptr, &buf);
  EXPECT_TRUE(got.empty());
  buf.append(fa.substr(30) + fb);  // 剩余 + 完整下一帧
  codec.onMessage(nullptr, &buf);
  ASSERT_EQ(got.size(), 2u);
}

TEST(Codec, EmptyBody) {  // 空 body 合法（proto3 全默认值）
  pb::Heartbeat h;
  std::string frame = ProtobufCodec::encode(MsgId::kHeartbeat, h);
  EXPECT_EQ(frame.size(), 6u);  // 4 + 2 + 0
  std::vector<Got> got;
  auto codec = makeCodec(&got);
  Buffer buf;
  buf.append(frame);
  codec.onMessage(nullptr, &buf);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_TRUE(got[0].body.empty());
}

TEST(Codec, BigEndianHeader) {  // 帧头必须是网络字节序
  pb::EchoMsg m;
  m.set_payload("x");
  std::string f = ProtobufCodec::encode(MsgId::kEcho, m);
  uint32_t len = (uint8_t(f[0]) << 24) | (uint8_t(f[1]) << 16) |
                 (uint8_t(f[2]) << 8) | uint8_t(f[3]);
  EXPECT_EQ(len, f.size() - 4);
  uint16_t id = (uint8_t(f[4]) << 8) | uint8_t(f[5]);
  EXPECT_EQ(id, MsgId::kEcho);
}
