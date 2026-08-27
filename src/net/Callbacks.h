#pragma once
#include <functional>
#include <memory>

namespace gs::net {

class TcpConnection;
class Buffer;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;

}  // namespace gs::net
