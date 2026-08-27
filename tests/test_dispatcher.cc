#include <gtest/gtest.h>

#include "game.pb.h"
#include "proto/Dispatcher.h"
#include "proto/MsgId.h"
using namespace gs;
using proto::Dispatcher;
using proto::MsgId;

TEST(Dispatcher, RoutesToTypedHandler) {
  Dispatcher d;
  std::string gotPayload;
  int64_t gotMs = 0;
  d.registerHandler<pb::EchoMsg>(
      MsgId::kEcho, [&](const net::TcpConnectionPtr&, const pb::EchoMsg& m) {
        gotPayload = m.payload();
      });
  d.registerHandler<pb::Heartbeat>(
      MsgId::kHeartbeat,
      [&](const net::TcpConnectionPtr&, const pb::Heartbeat& m) {
        gotMs = m.client_ms();
      });
  pb::EchoMsg e;
  e.set_payload("route-me");
  std::string body = e.SerializeAsString();
  d.onRawMessage(nullptr, MsgId::kEcho, body.data(), body.size());
  pb::Heartbeat h;
  h.set_client_ms(42);
  body = h.SerializeAsString();
  d.onRawMessage(nullptr, MsgId::kHeartbeat, body.data(), body.size());
  EXPECT_EQ(gotPayload, "route-me");
  EXPECT_EQ(gotMs, 42);
}

TEST(Dispatcher, UnknownMsgGoesDefault) {
  Dispatcher d;
  uint16_t unknownId = 0;
  d.setDefaultHandler(
      [&](const net::TcpConnectionPtr&, uint16_t id) { unknownId = id; });
  d.onRawMessage(nullptr, 999, "", 0);
  EXPECT_EQ(unknownId, 999);
}
