#pragma once
#include <string>
#include <unordered_map>

#include "net/Acceptor.h"
#include "net/Callbacks.h"
#include "net/EventLoopThreadPool.h"

namespace gs::net {

// 组装件：Acceptor(主 Reactor) + EventLoopThreadPool(从 Reactor) + 连接表。
// 用法：设回调 → setThreadNum → start() → baseLoop->loop()
class TcpServer {
 public:
  TcpServer(EventLoop* baseLoop, const InetAddress& listenAddr,
            std::string name);
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  void setThreadNum(int n) { threadNum_ = n; }
  void setConnectionCallback(ConnectionCallback cb) {
    connectionCb_ = std::move(cb);
  }
  void setMessageCallback(MessageCallback cb) { messageCb_ = std::move(cb); }
  void start();

 private:
  void newConnection(int sockfd, const InetAddress& peerAddr);  // baseLoop 线程
  void removeConnection(const TcpConnectionPtr& conn);  // IO 线程 → 转 baseLoop

  EventLoop* baseLoop_;
  const std::string name_;
  Acceptor acceptor_;
  EventLoopThreadPool threadPool_;
  int threadNum_ = 0;
  int nextConnId_ = 1;
  ConnectionCallback connectionCb_;
  MessageCallback messageCb_;
  // 仅 baseLoop 线程访问
  std::unordered_map<std::string, TcpConnectionPtr> connections_;
};

}  // namespace gs::net
