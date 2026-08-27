#include "net/Buffer.h"

#include <sys/uio.h>

#include <cassert>
#include <cerrno>

namespace gs::net {

void Buffer::retrieve(size_t len) {
  assert(len <= readableBytes());
  if (len < readableBytes()) {
    readerIndex_ += len;
  } else {
    retrieveAll();
  }
}

std::string Buffer::retrieveAllAsString() {
  std::string s(peek(), readableBytes());
  retrieveAll();
  return s;
}

void Buffer::ensureWritable(size_t len) {
  if (writableBytes() >= len) return;
  // 前面已读空间 + 尾部空间够用就腾挪，否则扩容
  if (writableBytes() + readerIndex_ - kCheapPrepend < len) {
    buffer_.resize(writerIndex_ + len);
  } else {
    size_t readable = readableBytes();
    std::copy(begin() + readerIndex_, begin() + writerIndex_,
              begin() + kCheapPrepend);
    readerIndex_ = kCheapPrepend;
    writerIndex_ = readerIndex_ + readable;
  }
}

void Buffer::append(const char* data, size_t len) {
  ensureWritable(len);
  std::copy(data, data + len, begin() + writerIndex_);
  writerIndex_ += len;
}

ssize_t Buffer::readFd(int fd, int* savedErrno) {
  char extrabuf[65536];  // 栈上备用区
  struct iovec vec[2];
  size_t writable = writableBytes();
  vec[0].iov_base = begin() + writerIndex_;
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof(extrabuf);
  ssize_t n = ::readv(fd, vec, 2);
  if (n < 0) {
    *savedErrno = errno;
  } else if (static_cast<size_t>(n) <= writable) {
    writerIndex_ += n;
  } else {
    writerIndex_ = buffer_.size();
    append(extrabuf, n - writable);
  }
  return n;
}

}  // namespace gs::net
