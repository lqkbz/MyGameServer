#include <gtest/gtest.h>

#include "storage/MySqlStore.h"
using gs::storage::MySqlStore;

namespace {
#define REQUIRE_MYSQL(m)                                        \
  if (!(m).connect("127.0.0.1", "gs", "gs123", "gs") ||         \
      !(m).ensureSchema())                                      \
  GTEST_SKIP() << "mysql unavailable"
}  // namespace

TEST(MySql, ConnectAndSchema) {
  MySqlStore m;
  REQUIRE_MYSQL(m);
}

TEST(MySql, RecordAndQuery) {
  MySqlStore m;
  REQUIRE_MYSQL(m);
  m.removeAccount("test_w");  // 清理旧数据保证可重复跑
  m.removeAccount("test_l");
  EXPECT_FALSE(m.getRecord("test_w").has_value());

  ASSERT_TRUE(m.recordResult("test_w", "test_l"));
  ASSERT_TRUE(m.recordResult("test_w", "test_l"));
  auto w = m.getRecord("test_w");
  auto l = m.getRecord("test_l");
  ASSERT_TRUE(w.has_value());
  ASSERT_TRUE(l.has_value());
  EXPECT_EQ(w->first, 2);   // wins
  EXPECT_EQ(w->second, 0);  // losses
  EXPECT_EQ(l->first, 0);
  EXPECT_EQ(l->second, 2);
  m.removeAccount("test_w");
  m.removeAccount("test_l");
}

TEST(MySql, EscapesInput) {  // 注入向量当普通账号名处理
  MySqlStore m;
  REQUIRE_MYSQL(m);
  std::string evil = "x'; DROP TABLE players; --";
  m.removeAccount(evil);
  ASSERT_TRUE(m.recordResult(evil, "test_v"));
  auto r = m.getRecord(evil);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->first, 1);
  m.removeAccount(evil);
  m.removeAccount("test_v");
}
