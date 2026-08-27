#include "net/TcpServer.h"

#include "net/EventLoop.h"
#include "net/TcpConnection.h"

namespace gs::net {

TcpServer::TcpServer(EventLoop* baseLoop, const InetAddress& listenAddr,
                     std::string name)
    : baseLoop_(baseLoop),
      name_(std::move(name)),
      acceptor_(baseLoop, listenAddr),
      threadPool_(baseLoop) {
  acceptor_.setNewConnectionCallback(
      [this](int fd, const InetAddress& peer) { newConnection(fd, peer); });
}

TcpServer::~TcpServer() {
  for (auto& [n, conn] : connections_) {
    TcpConnectionPtr c = conn;
    c->getLoop()->runInLoop([c] { c->connectDestroyed(); });
  }
}

void TcpServer::start() {
  threadPool_.start(threadNum_);
  baseLoop_->runInLoop([this] { acceptor_.listen(); });
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr) {
  // round-robin 选一个 IO loop，连接终生绑定它（one loop per thread）
  EventLoop* ioLoop = threadPool_.getNextLoop();
  std::string connName = name_ + "-" + std::to_string(nextConnId_++);
  auto conn =
      std::make_shared<TcpConnection>(ioLoop, connName, sockfd, peerAddr);
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCb_);
  conn->setMessageCallback(messageCb_);
  conn->setCloseCallback(
      [this](const TcpConnectionPtr& c) { removeConnection(c); });
  // channel 注册必须在连接所属 IO 线程做
  ioLoop->runInLoop([conn] { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
  // 从 IO 线程回到 baseLoop 线程改连接表（表只被 baseLoop 碰，无锁）
  baseLoop_->runInLoop([this, conn] {
    connections_.erase(conn->name());
    // 最后再回到 IO 线程摘 channel；conn 值捕获续命到清理完成
    conn->getLoop()->queueInLoop([conn] { conn->connectDestroyed(); });
  });
}

}  // namespace gs::net
