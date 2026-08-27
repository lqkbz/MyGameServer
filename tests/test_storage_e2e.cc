#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "e2e_util.h"
#include "game/GameServer.h"
#include "storage/MySqlStore.h"
#include "storage/RedisClient.h"
using namespace gs;
using proto::MsgId;
using test::matchUp;
using test::QuitGuard;
using test::TestClient;

// 打一局 → 存储线程异步落库:MySQL 战绩、Redis 排行榜、LeaderboardResp。
// redis/mysql 任一不可用则 SKIP。
TEST(StorageE2E, BattleResultPersisted) {
  storage::RedisClient redis;
  storage::MySqlStore mysql;
  if (!redis.connect("127.0.0.1", 6379) || !redis.ping() ||
      !mysql.connect("127.0.0.1", "gs", "gs123", "gs") ||
      !mysql.ensureSchema()) {
    GTEST_SKIP() << "redis/mysql unavailable";
  }
  // 隔离清理:独立排行榜 key + 测试账号
  redis.del("test:lb:e2e");
  mysql.removeAccount("sw_alice");
  mysql.removeAccount("sw_bob");

  game::StorageOptions opts;
  opts.enabled = true;
  opts.leaderboardKey = "test:lb:e2e";

  net::EventLoop loop;
  game::GameServer server(&loop, 15605, 2, 2, opts);
  server.start();

  bool ok = false;
  std::thread driver([&] {
    QuitGuard qg{&loop};
    TestClient a, b;
    ASSERT_TRUE(a.connect(15605));
    ASSERT_TRUE(b.connect(15605));
    pb::LoginResp ra, rb;
    ASSERT_NE(matchUp(a, b, "sw_alice", "sw_bob", &ra, &rb), 0u);

    pb::PlayerInput in;  // alice 平推获胜
    in.set_seq(1);
    in.set_move_x(1);
    in.set_attack(true);
    a.send(MsgId::kPlayerInput, in);
    std::string body;
    ASSERT_TRUE(a.waitFor(MsgId::kBattleEnd, &body));
    pb::BattleEnd end;
    end.ParseFromString(body);
    ASSERT_EQ(end.winner_id(), ra.player_id());

    // 落库是异步的:轮询等待(上限 3s)
    bool persisted = false;
    for (int i = 0; i < 30; ++i) {
      auto rec = mysql.getRecord("sw_alice");
      if (rec && rec->first >= 1) {
        persisted = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(persisted) << "mysql record not written in 3s";
    auto lose = mysql.getRecord("sw_bob");
    ASSERT_TRUE(lose.has_value());
    EXPECT_EQ(lose->second, 1);  // bob 负 +1

    auto top = redis.ztopN("test:lb:e2e", 10);
    ASSERT_FALSE(top.empty());
    EXPECT_EQ(top[0].first, "sw_alice");
    EXPECT_DOUBLE_EQ(top[0].second, 1.0);

    // 排行榜查询协议
    pb::LeaderboardReq lreq;
    lreq.set_top_n(5);
    a.send(MsgId::kLeaderboardReq, lreq);
    ASSERT_TRUE(a.waitFor(MsgId::kLeaderboardResp, &body));
    pb::LeaderboardResp lresp;
    lresp.ParseFromString(body);
    ASSERT_GE(lresp.entries_size(), 1);
    EXPECT_EQ(lresp.entries(0).account(), "sw_alice");

    // 在线状态:对局后仍在线
    EXPECT_TRUE(redis.exists("online:sw_alice"));
    ok = true;
    a.close();
    b.close();
  });
  loop.loop();
  driver.join();
  ASSERT_TRUE(ok);

  // 断线后 online 键被异步清掉(轮询上限 2s)
  bool offline = false;
  for (int i = 0; i < 20; ++i) {
    if (!redis.exists("online:sw_alice")) {
      offline = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  EXPECT_TRUE(offline);

  redis.del("test:lb:e2e");
  mysql.removeAccount("sw_alice");
  mysql.removeAccount("sw_bob");
}
