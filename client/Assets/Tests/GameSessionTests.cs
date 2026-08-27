using System.Collections.Generic;
using Google.Protobuf;
using Gs.Pb;
using GsClient.Game;
using GsClient.Net;
using NUnit.Framework;

namespace GsClient.Tests
{
    // GameSession 状态机单测:send 用捕获列表替身,消息用 HandleMessage 注入
    public class GameSessionTests
    {
        private List<(ushort id, IMessage msg)> sent_;
        private GameSession s_;

        [SetUp]
        public void SetUp()
        {
            sent_ = new List<(ushort, IMessage)>();
            s_ = new GameSession((id, msg) => sent_.Add((id, msg)));
        }

        // 便捷:把 pb 消息编码成 body 再喂进 HandleMessage(模拟收包路径)
        private void Feed(ushort id, IMessage msg) => s_.HandleMessage(id, msg.ToByteArray());

        private void LoginTo(ulong pid, string token)
        {
            s_.TransportConnected("alice");
            Feed(MsgIds.LoginResp, new LoginResp { Code = 0, PlayerId = pid, SessionToken = token });
        }

        [Test]
        public void Login_SendsReq_ThenLobby()
        {
            s_.TransportConnected("alice");
            Assert.AreEqual(SessionState.LoggingIn, s_.State);
            Assert.AreEqual(MsgIds.LoginReq, sent_[0].id);
            Assert.AreEqual("alice", ((LoginReq)sent_[0].msg).Account);

            Feed(MsgIds.LoginResp, new LoginResp { Code = 0, PlayerId = 7, SessionToken = "tok" });
            Assert.AreEqual(SessionState.Lobby, s_.State);
            Assert.AreEqual(7UL, s_.PlayerId);
        }

        [Test]
        public void Match_EnterRoom_ToBattle()
        {
            LoginTo(7, "tok");
            s_.RequestMatch();
            Assert.AreEqual(SessionState.Matching, s_.State);
            Assert.AreEqual(MsgIds.MatchReq, sent_[1].id);

            var enter = new EnterRoom { RoomId = 3 };
            enter.PlayerIds.Add(7UL);
            enter.PlayerIds.Add(8UL);
            Feed(MsgIds.EnterRoom, enter);
            Assert.AreEqual(SessionState.InBattle, s_.State);
            Assert.AreEqual(3UL, s_.RoomId);
        }

        [Test]
        public void MatchTimeout_RoomIdZero_BackToLobby()
        {
            LoginTo(7, "tok");
            s_.RequestMatch();
            Feed(MsgIds.EnterRoom, new EnterRoom { RoomId = 0 }); // 约定:匹配失败
            Assert.AreEqual(SessionState.Lobby, s_.State);
        }

        [Test]
        public void BattleEnd_FiresEvent()
        {
            LoginTo(7, "tok");
            s_.RequestMatch();
            var enter = new EnterRoom { RoomId = 3 };
            enter.PlayerIds.Add(7UL);
            enter.PlayerIds.Add(8UL);
            Feed(MsgIds.EnterRoom, enter);

            ulong winner = 0;
            s_.OnBattleEnd += e => winner = e.WinnerId;
            Feed(MsgIds.BattleEnd, new BattleEnd { WinnerId = 8 });
            Assert.AreEqual(SessionState.BattleEnded, s_.State);
            Assert.AreEqual(8UL, winner);
        }

        [Test]
        public void Reconnect_UsesToken_BackToBattle()
        {
            LoginTo(7, "tok");
            s_.RequestMatch();
            var enter = new EnterRoom { RoomId = 3 };
            enter.PlayerIds.Add(7UL);
            enter.PlayerIds.Add(8UL);
            Feed(MsgIds.EnterRoom, enter);

            s_.TransportDisconnected();
            Assert.AreEqual(SessionState.Disconnected, s_.State);
            Assert.IsTrue(s_.CanReconnect);

            sent_.Clear();
            s_.TransportConnected("alice"); // 有 token → 走重连
            Assert.AreEqual(MsgIds.ReconnectReq, sent_[0].id);
            var req = (ReconnectReq)sent_[0].msg;
            Assert.AreEqual(7UL, req.PlayerId);
            Assert.AreEqual("tok", req.SessionToken);

            // 重连成功且此前在房间 → 直接回战场(服务端 rebind 后补全量快照)
            Feed(MsgIds.LoginResp, new LoginResp { Code = 0, PlayerId = 7, SessionToken = "tok" });
            Assert.AreEqual(SessionState.InBattle, s_.State);
        }

        [Test]
        public void LoginFail_FiresError_ClearsToken()
        {
            LoginTo(7, "tok");
            s_.TransportDisconnected();
            sent_.Clear();
            string err = null;
            s_.OnError += e => err = e;

            s_.TransportConnected("alice");
            Feed(MsgIds.LoginResp, new LoginResp { Code = 1 }); // token 失效(如 30s 过期)
            Assert.IsNotNull(err);
            Assert.IsFalse(s_.CanReconnect); // 清 token,下次走全新登录
        }

        [Test]
        public void Heartbeat_Every2Seconds()
        {
            LoginTo(7, "tok");
            sent_.Clear();
            s_.Tick(1000);
            Assert.AreEqual(1, sent_.Count); // 首跳立即发
            Assert.AreEqual(MsgIds.Heartbeat, sent_[0].id);
            s_.Tick(2000);
            Assert.AreEqual(1, sent_.Count); // 未到 2s 不发
            s_.Tick(3000);
            Assert.AreEqual(2, sent_.Count);
        }

        [Test]
        public void SendInput_OnlyInBattle()
        {
            LoginTo(7, "tok");
            sent_.Clear();
            s_.SendInput(1f, 0f, false); // Lobby 状态:忽略
            Assert.AreEqual(0, sent_.Count);

            s_.RequestMatch();
            var enter = new EnterRoom { RoomId = 3 };
            enter.PlayerIds.Add(7UL);
            enter.PlayerIds.Add(8UL);
            Feed(MsgIds.EnterRoom, enter);
            sent_.Clear();

            s_.SendInput(1f, 0f, true);
            s_.SendInput(0f, 1f, false);
            Assert.AreEqual(2, sent_.Count);
            var i1 = (PlayerInput)sent_[0].msg;
            var i2 = (PlayerInput)sent_[1].msg;
            Assert.IsTrue(i1.Attack);
            Assert.AreEqual(i1.Seq + 1, i2.Seq); // seq 单调递增
        }

        [Test]
        public void BackToLobby_OnlyFromBattleEnded()
        {
            LoginTo(7, "tok");
            s_.BackToLobby(); // Lobby 状态下调用:无效果
            Assert.AreEqual(SessionState.Lobby, s_.State);

            s_.RequestMatch();
            var enter = new EnterRoom { RoomId = 3 };
            enter.PlayerIds.Add(7UL);
            enter.PlayerIds.Add(8UL);
            Feed(MsgIds.EnterRoom, enter);
            s_.BackToLobby(); // 战斗中调用:无效果
            Assert.AreEqual(SessionState.InBattle, s_.State);

            Feed(MsgIds.BattleEnd, new BattleEnd { WinnerId = 8 });
            s_.BackToLobby(); // 结算后:回大厅,可再匹配
            Assert.AreEqual(SessionState.Lobby, s_.State);
            s_.RequestMatch();
            Assert.AreEqual(SessionState.Matching, s_.State);
        }

        [Test]
        public void Snapshot_ForwardedInBattle()
        {
            LoginTo(7, "tok");
            s_.RequestMatch();
            var enter = new EnterRoom { RoomId = 3 };
            enter.PlayerIds.Add(7UL);
            enter.PlayerIds.Add(8UL);
            Feed(MsgIds.EnterRoom, enter);

            StateSnapshot got = null;
            s_.OnSnapshot += snap => got = snap;
            var s = new StateSnapshot { Tick = 12, Full = true };
            s.Players.Add(new PlayerState { PlayerId = 7, X = 10, Y = 20, Hp = 100 });
            Feed(MsgIds.StateSnapshot, s);
            Assert.IsNotNull(got);
            Assert.AreEqual(12u, got.Tick);
        }
    }
}
