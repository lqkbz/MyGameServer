#include "net/TcpConnection.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "net/EventLoop.h"

namespace gs::net {

TcpConnection::TcpConnection(EventLoop* loop, std::string name, int sockfd,
                             const InetAddress& peerAddr)
    : loop_(loop),
      name_(std::move(name)),
      socket_(sockfd),
      channel_(loop, sockfd),
      peerAddr_(peerAddr) {
  channel_.setReadCallback([this] { handleRead(); });
  channel_.setWriteCallback([this] { handleWrite(); });
  channel_.setCloseCallback([this] { handleClose(); });
  channel_.setErrorCallback([this] { handleError(); });
  socket_.setNoDelay(true);  // 游戏服务器：低延迟优先，关 Nagle
}

TcpConnection::~TcpConnection() = default;

void TcpConnection::connectEstablished() {
  state_ = kConnected;
  channel_.enableReading();
  if (connectionCb_) connectionCb_(shared_from_this());
}

void TcpConnection::connectDestroyed() {
  if (state_ == kConnected) {
    state_ = kDisconnected;
    channel_.disableAll();
    if (connectionCb_) connectionCb_(shared_from_this());
  }
  loop_->removeChannel(&channel_);
}

void TcpConnection::handleRead() {
  // ET：必须循环读到 EAGAIN，否则剩余数据不会再触发事件
  bool peerClosed = false;
  while (true) {
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_.fd(), &savedErrno);
    if (n > 0) {
      continue;  // 还可能有数据，继续读
    } else if (n == 0) {
      // 对端关闭：不能立刻 close——本轮可能刚读进了最后一批数据，
      // 必须先经 messageCb_ 投递，否则"发完就关"的对端数据会丢
      peerClosed = true;
      break;
    } else {
      if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) break;
      if (savedErrno == EINTR) continue;
      handleError();
      return;
    }
  }
  if (inputBuffer_.readableBytes() > 0 && messageCb_) {
    messageCb_(shared_from_this(), &inputBuffer_);
  }
  if (peerClosed) handleClose();
}

void TcpConnection::handleWrite() {
  if (!channel_.isWriting()) return;
  // ET：循环写到 EAGAIN 或写完
  while (outputBuffer_.readableBytes() > 0) {
    ssize_t n = ::write(channel_.fd(), outputBuffer_.peek(),
                        outputBuffer_.readableBytes());
    if (n > 0) {
      outputBuffer_.retrieve(n);
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;  // 等下次 EPOLLOUT
      if (errno == EINTR) continue;
      handleError();
      return;
    }
  }
  // 写完必须取消关注 EPOLLOUT，避免无谓唤醒
  channel_.disableWriting();
  if (state_ == kDisconnecting) shutdownInLoop();
}

void TcpConnection::handleClose() {
  state_ = kDisconnected;
  channel_.disableAll();
  TcpConnectionPtr guard = shared_from_this();  // 防止回调中 this 被销毁
  if (connectionCb_) connectionCb_(guard);
  if (closeCb_) closeCb_(guard);  // 通知 TcpServer 摘除连接表
}

void TcpConnection::handleError() {
  int err = 0;
  socklen_t len = sizeof err;
  ::getsockopt(channel_.fd(), SOL_SOCKET, SO_ERROR, &err, &len);
  // 学习项目：错误直接走关闭路径
  handleClose();
}

void TcpConnection::send(const std::string& msg) {
  if (state_ != kConnected) return;
  // 跨线程安全：统一转到 loop 线程执行，值捕获拷贝一次 msg
  if (loop_->isInLoopThread()) {
    sendInLoop(msg);
  } else {
    auto self = shared_from_this();
    loop_->runInLoop([self, msg] { self->sendInLoop(msg); });
  }
}

void TcpConnection::sendInLoop(const std::string& msg) {
  if (state_ == kDisconnected) return;
  size_t remaining = msg.size();
  ssize_t nwrote = 0;
  // 输出缓冲为空时先直接写（大多数情况一次写完，零排队）
  if (!channel_.isWriting() && outputBuffer_.readableBytes() == 0) {
    nwrote = ::write(channel_.fd(), msg.data(), msg.size());
    if (nwrote >= 0) {
      remaining = msg.size() - nwrote;
    } else {
      nwrote = 0;
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        handleError();
        return;
      }
    }
  }
  // 没写完的进缓冲，关注 EPOLLOUT 续写
  if (remaining > 0) {
    outputBuffer_.append(msg.data() + nwrote, remaining);
    if (!channel_.isWriting()) channel_.enableWriting();
  }
}

void TcpConnection::shutdown() {
  if (state_ == kConnected) {
    state_ = kDisconnecting;
    auto self = shared_from_this();
    loop_->runInLoop([self] { self->shutdownInLoop(); });
  }
}

void TcpConnection::shutdownInLoop() {
  // 还在写则等 handleWrite 写完后再 shutdown（见 handleWrite 尾部）
  if (!channel_.isWriting()) ::shutdown(channel_.fd(), SHUT_WR);
}

}  // namespace gs::net
