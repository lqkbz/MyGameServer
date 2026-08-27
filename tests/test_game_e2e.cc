#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "e2e_util.h"
#include "game/GameServer.h"
using namespace gs;
using proto::MsgId;
using test::matchUp;
using test::QuitGuard;
using test::TestClient;

// 完整一局:登录→匹配→进房→A 平推+攻击→B 挂机→BattleEnd(winner=A)
TEST(GameE2E, FullBattle) {
  net::EventLoop loop;
  game::GameServer server(&loop, 15601, 2, 2);
  server.start();

  bool ok = false;
  uint64_t winner = 0, apid = 0;
  std::thread driver([&] {
    QuitGuard qg{&loop};
    TestClient a, b;
    ASSERT_TRUE(a.connect(15601));
    ASSERT_TRUE(b.connect(15601));
    pb::LoginResp ra, rb;
    uint64_t roomId = matchUp(a, b, "alice", "bob", &ra, &rb);
    ASSERT_NE(roomId, 0u);
    apid = ra.player_id();

    // 进房后应先收到全量快照(两名玩家)
    std::string body;
    ASSERT_TRUE(a.waitFor(MsgId::kStateSnapshot, &body));
    pb::StateSnapshot snap;
    snap.ParseFromString(body);
    EXPECT_TRUE(snap.full());
    EXPECT_EQ(snap.players_size(), 2);

    // A:状态式输入只发一次——朝右平推+按住攻击,服务器每 tick 持续生效
    pb::PlayerInput in;
    in.set_seq(1);
    in.set_move_x(1);
    in.set_attack(true);
    a.send(MsgId::kPlayerInput, in);

    ASSERT_TRUE(a.waitFor(MsgId::kBattleEnd, &body));
    pb::BattleEnd end;
    end.ParseFromString(body);
    winner = end.winner_id();
    ASSERT_TRUE(b.waitFor(MsgId::kBattleEnd, &body));
    ok = true;
    a.close();
    b.close();
  });
  loop.loop();
  driver.join();
  ASSERT_TRUE(ok);
  EXPECT_EQ(winner, apid);
}

// 断线重连:B 断开后带 token 重连,收到 EnterRoom + full 快照
TEST(GameE2E, Reconnect) {
  net::EventLoop loop;
  game::GameServer server(&loop, 15602, 2, 2);
  server.start();

  bool ok = false;
  std::thread driver([&] {
    QuitGuard qg{&loop};
    TestClient a, b;
    ASSERT_TRUE(a.connect(15602));
    ASSERT_TRUE(b.connect(15602));
    pb::LoginResp ra, rb;
    ASSERT_NE(matchUp(a, b, "alice", "bob", &ra, &rb), 0u);

    b.close();  // 模拟断线
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TestClient b2;
    ASSERT_TRUE(b2.connect(15602));
    pb::ReconnectReq rc;
    rc.set_player_id(rb.player_id());
    rc.set_session_token(rb.session_token());
    b2.send(MsgId::kReconnectReq, rc);
    std::string body;
    ASSERT_TRUE(b2.waitFor(MsgId::kLoginResp, &body));
    pb::LoginResp resp;
    resp.ParseFromString(body);
    ASSERT_EQ(resp.code(), 0);
    ASSERT_TRUE(b2.waitFor(MsgId::kEnterRoom, &body));
    ASSERT_TRUE(b2.waitFor(MsgId::kStateSnapshot, &body));
    pb::StateSnapshot snap;
    snap.ParseFromString(body);
    EXPECT_TRUE(snap.full());  // 重连补全量
    EXPECT_EQ(snap.players_size(), 2);
    ok = true;
    a.close();
    b2.close();
  });
  loop.loop();
  driver.join();
  ASSERT_TRUE(ok);
}

// 错误 token 重连被拒
TEST(GameE2E, ReconnectBadTokenRejected) {
  net::EventLoop loop;
  game::GameServer server(&loop, 15603, 1, 1);
  server.start();

  bool ok = false;
  std::thread driver([&] {
    QuitGuard qg{&loop};
    TestClient a;
    ASSERT_TRUE(a.connect(15603));
    pb::LoginResp ra;
    ASSERT_TRUE(a.login("carol", &ra));
    a.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestClient a2;
    ASSERT_TRUE(a2.connect(15603));
    pb::ReconnectReq rc;
    rc.set_player_id(ra.player_id());
    rc.set_session_token("wrong-token");
    a2.send(MsgId::kReconnectReq, rc);
    std::string body;
    ASSERT_TRUE(a2.waitFor(MsgId::kLoginResp, &body));
    pb::LoginResp resp;
    resp.ParseFromString(body);
    EXPECT_EQ(resp.code(), 1);
    ok = true;
    a2.close();
  });
  loop.loop();
  driver.join();
  ASSERT_TRUE(ok);
}

// 心跳原样回显(客户端测 RTT 用)
TEST(GameE2E, HeartbeatEcho) {
  net::EventLoop loop;
  game::GameServer server(&loop, 15604, 1, 1);
  server.start();

  bool ok = false;
  std::thread driver([&] {
    QuitGuard qg{&loop};
    TestClient a;
    ASSERT_TRUE(a.connect(15604));
    pb::LoginResp ra;
    ASSERT_TRUE(a.login("dave", &ra));
    pb::Heartbeat hb;
    hb.set_client_ms(123456);
    a.send(MsgId::kHeartbeat, hb);
    std::string body;
    ASSERT_TRUE(a.waitFor(MsgId::kHeartbeat, &body));
    pb::Heartbeat back;
    back.ParseFromString(body);
    EXPECT_EQ(back.client_ms(), 123456);
    ok = true;
    a.close();
  });
  loop.loop();
  driver.join();
  ASSERT_TRUE(ok);
}
