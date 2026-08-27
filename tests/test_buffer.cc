#include <gtest/gtest.h>
#include <unistd.h>
#include "net/Buffer.h"
using gs::net::Buffer;

TEST(Buffer, AppendRetrieve) {
  Buffer buf;
  EXPECT_EQ(buf.readableBytes(), 0u);
  std::string s(200, 'x');
  buf.append(s);
  EXPECT_EQ(buf.readableBytes(), 200u);
  buf.retrieve(50);
  EXPECT_EQ(buf.readableBytes(), 150u);
  EXPECT_EQ(buf.retrieveAllAsString(), std::string(150, 'x'));
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(Buffer, Grow) {
  Buffer buf;
  buf.append(std::string(400, 'y'));
  buf.retrieve(300);
  buf.append(std::string(1000, 'z'));  // 触发内部腾挪或扩容
  EXPECT_EQ(buf.readableBytes(), 1100u);
  buf.retrieve(100);
  EXPECT_EQ(buf.retrieveAllAsString(), std::string(1000, 'z'));
}

TEST(Buffer, ReadFd) {
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);
  ::write(fds[1], "hello", 5);
  Buffer buf;
  int savedErrno = 0;
  ssize_t n = buf.readFd(fds[0], &savedErrno);
  EXPECT_EQ(n, 5);
  EXPECT_EQ(buf.retrieveAllAsString(), "hello");
  ::close(fds[0]);
  ::close(fds[1]);
}
