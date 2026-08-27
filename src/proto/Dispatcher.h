#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "net/Callbacks.h"

namespace gs::proto {

// msgId → 强类型回调表。registerHandler<M> 在注册处捕获具体 pb 类型，
// 内部包一层 Parse，业务代码拿到的直接是解析好的消息对象（类型擦除技巧）。
class Dispatcher {
 public:
  using DefaultHandler =
      std::function<void(const net::TcpConnectionPtr&, uint16_t msgId)>;

  template <typename M>
  void registerHandler(
      uint16_t msgId,
      std::function<void(const net::TcpConnectionPtr&, const M&)> h) {
    handlers_[msgId] = [h = std::move(h), this](
                           const net::TcpConnectionPtr& conn, uint16_t id,
                           const char* data, size_t len) {
      M msg;
      if (msg.ParseFromArray(data, static_cast<int>(len))) {
        h(conn, msg);
      } else if (defaultHandler_) {  // 解析失败按未知消息处理
        defaultHandler_(conn, id);
      }
    };
  }

  void setDefaultHandler(DefaultHandler h) { defaultHandler_ = std::move(h); }

  // 直接可挂 ProtobufCodec 的 RawMessageCallback
  void onRawMessage(const net::TcpConnectionPtr& conn, uint16_t msgId,
                    const char* data, size_t len) {
    auto it = handlers_.find(msgId);
    if (it != handlers_.end()) {
      it->second(conn, msgId, data, len);
    } else if (defaultHandler_) {
      defaultHandler_(conn, msgId);
    }
  }

 private:
  using RawHandler = std::function<void(const net::TcpConnectionPtr&, uint16_t,
                                        const char*, size_t)>;
  std::unordered_map<uint16_t, RawHandler> handlers_;
  DefaultHandler defaultHandler_;
};

}  // namespace gs::proto
