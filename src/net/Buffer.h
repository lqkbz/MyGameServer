#pragma once
#include <sys/types.h>

#include <cstddef>
#include <string>
#include <vector>

namespace gs::net {

// 应用层缓冲区。ET 模式下必须一次读到 EAGAIN，读到的数据先存这里；
// 写不完的数据也暂存这里等 EPOLLOUT。
// 内存布局: | prependable | readable | writable |
// prepend 区预留 8 字节，M2 编码帧长度时可在头部原地写入，免一次拷贝。
class Buffer {
 public:
  static constexpr size_t kCheapPrepend = 8;
  static constexpr size_t kInitialSize = 1024;

  Buffer()
      : buffer_(kCheapPrepend + kInitialSize),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {}

  size_t readableBytes() const { return writerIndex_ - readerIndex_; }
  size_t writableBytes() const { return buffer_.size() - writerIndex_; }
  const char* peek() const { return begin() + readerIndex_; }

  void retrieve(size_t len);
  void retrieveAll() { readerIndex_ = writerIndex_ = kCheapPrepend; }
  std::string retrieveAllAsString();

  void append(const char* data, size_t len);
  void append(const std::string& s) { append(s.data(), s.size()); }

  // readv 分散读：不够时溢出到 64KB 栈上空间再 append 回来，
  // 兼顾大报文吞吐与小连接内存占用（muduo 经典技巧）。
  ssize_t readFd(int fd, int* savedErrno);

 private:
  char* begin() { return buffer_.data(); }
  const char* begin() const { return buffer_.data(); }
  void ensureWritable(size_t len);

  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;
};

}  // namespace gs::net
