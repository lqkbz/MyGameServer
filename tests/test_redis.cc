#include <gtest/gtest.h>

#include "storage/RedisClient.h"
using gs::storage::RedisClient;

namespace {
// redis 不可用时跳过(CI/无服务环境)
#define REQUIRE_REDIS(r)                              \
  if (!(r).connect("127.0.0.1", 6379) || !(r).ping()) \
  GTEST_SKIP() << "redis unavailable"
}  // namespace

TEST(Redis, Ping) {
  RedisClient r;
  REQUIRE_REDIS(r);
  EXPECT_TRUE(r.ping());
}

TEST(Redis, ZincrbyAndTopN) {
  RedisClient r;
  REQUIRE_REDIS(r);
  r.del("test:lb");
  EXPECT_TRUE(r.zincrby("test:lb", "alice", 3));
  EXPECT_TRUE(r.zincrby("test:lb", "bob", 1));
  EXPECT_TRUE(r.zincrby("test:lb", "alice", 2));  // alice=5
  auto top = r.ztopN("test:lb", 10);
  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0].first, "alice");  // 降序
  EXPECT_DOUBLE_EQ(top[0].second, 5.0);
  EXPECT_EQ(top[1].first, "bob");
  r.del("test:lb");
}

TEST(Redis, SetExistsDel) {
  RedisClient r;
  REQUIRE_REDIS(r);
  r.del("test:online:x");
  EXPECT_FALSE(r.exists("test:online:x"));
  EXPECT_TRUE(r.set("test:online:x", "1"));
  EXPECT_TRUE(r.exists("test:online:x"));
  EXPECT_TRUE(r.del("test:online:x"));
  EXPECT_FALSE(r.exists("test:online:x"));
}
