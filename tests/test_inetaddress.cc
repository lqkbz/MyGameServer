#include <gtest/gtest.h>
#include "net/InetAddress.h"
using gs::net::InetAddress;

TEST(InetAddress, ToIpPort) {
  InetAddress addr(8888, "127.0.0.1");
  EXPECT_EQ(addr.toIpPort(), "127.0.0.1:8888");
}

TEST(InetAddress, AnyAddr) {
  InetAddress addr(9999);
  EXPECT_EQ(addr.toIpPort(), "0.0.0.0:9999");
}
