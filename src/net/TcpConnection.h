#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "net/Buffer.h"
#include "net/Callbacks.h"
#include "net/Channel.h"
#include "net/InetAddress.h"
#include "net/Socket.h"

namespace gs::net {

class EventLoop;

// 一条 TCP 连接的生命周期管理。
// 用 shared_ptr 管理 + enable_shared_from_this：
// 回调执行期间连接对象一定活着，解决"处理事件时连接被另一线程销毁"的经典竞态。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
  TcpConnection(EventLoop* loop, std::string name, int sockfd,
                const InetAddress& peerAddr);
  ~TcpConnection();
  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  EventLoop* getLoop() const { return loop_; }
  const std::string& name() const { return name_; }
  const InetAddress& peerAddress() const { return peerAddr_; }
  bool connected() const { return state_ == kConnected; }

  void send(const std::string& msg);  // 任意线程可调，内部转到 loop 线程
  void shutdown();                    // 关写端，等对端读完 FIN

  void setConnectionCallback(ConnectionCallback cb) {
    connectionCb_ = std::move(cb);
  }
  void setMessageCallback(MessageCallback cb) { messageCb_ = std::move(cb); }
  void setCloseCallback(CloseCallback cb) { closeCb_ = std::move(cb); }

  // 仅 TcpServer 调用（在 loop 线程）
  void connectEstablished();  // 注册 channel、回调用户
  void connectDestroyed();    // 摘除 channel、回调用户

 private:
  enum State { kConnecting, kConnected, kDisconnecting, kDisconnected };

  void handleRead();
  void handleWrite();
  void handleClose();
  void handleError();
  void sendInLoop(const std::string& msg);
  void shutdownInLoop();

  EventLoop* loop_;
  const std::string name_;
  std::atomic<State> state_{kConnecting};
  Socket socket_;
  Channel channel_;
  const InetAddress peerAddr_;
  Buffer inputBuffer_;
  Buffer outputBuffer_;
  ConnectionCallback connectionCb_;
  MessageCallback messageCb_;
  CloseCallback closeCb_;
};

}  // namespace gs::net
