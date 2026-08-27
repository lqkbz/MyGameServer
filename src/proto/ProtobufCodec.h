#pragma once
#include <google/protobuf/message.h>

#include <cstdint>
#include <functional>
#include <string>

#include "net/Callbacks.h"

namespace gs::net {
class Buffer;
}

namespace gs::proto {

// 帧格式（大端）: | len(4B) | msgId(2B) | protobuf body |
// len = 2 + body长度。TCP 是字节流，粘包/半包在这一层解决：
// 循环切帧，凑不齐一帧就留在 Buffer 里等下次数据到达。
class ProtobufCodec {
 public:
  using RawMessageCallback =
      std::function<void(const net::TcpConnectionPtr&, uint16_t msgId,
                         const char* data, size_t len)>;

  static constexpr size_t kHeaderLen = 4;
  static constexpr size_t kIdLen = 2;
  static constexpr size_t kMaxFrameLen = 64 * 1024;  // 超限视为恶意，断连

  explicit ProtobufCodec(RawMessageCallback cb) : cb_(std::move(cb)) {}

  void onMessage(const net::TcpConnectionPtr& conn, net::Buffer* buf);
  static std::string encode(uint16_t msgId,
                            const google::protobuf::Message& msg);
  void send(const net::TcpConnectionPtr& conn, uint16_t msgId,
            const google::protobuf::Message& msg);

 private:
  RawMessageCallback cb_;
};

}  // namespace gs::proto
