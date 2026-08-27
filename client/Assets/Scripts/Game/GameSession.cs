using System;
using Google.Protobuf;
using Gs.Pb;
using GsClient.Net;

namespace GsClient.Game
{
    public enum SessionState
    {
        Disconnected, // 初始/断线(可能持有重连 token)
        Connecting,   // TCP 连接中
        LoggingIn,    // 已连上,等 LoginResp
        Lobby,        // 登录成功,可发起匹配
        Matching,     // 匹配队列中
        InBattle,     // 房间对战中
        BattleEnded,  // 结算完成
    }

    // 游戏会话状态机(纯 C#,不依赖 Unity/socket,便于单测):
    // - 输入:Transport* 连接事件 + HandleMessage 收包 + 用户操作
    // - 输出:注入的 send 回调 + 领域事件
    // 服务端协议约定(见 GameServer.cc):
    //   LoginReq/ReconnectReq → LoginResp(code=0 成功);EnterRoom.room_id=0 表示匹配超时
    public class GameSession
    {
        public const long HeartbeatIntervalMs = 2000; // 服务端 10s 无心跳踢人

        private readonly Action<ushort, IMessage> send_;
        private uint inputSeq_;
        private long lastHeartbeatMs_ = long.MinValue;
        private bool wasInBattle_; // 断线时是否在房间(重连成功后直接回战场)

        public SessionState State { get; private set; } = SessionState.Disconnected;
        public ulong PlayerId { get; private set; }
        public string Token { get; private set; }
        public ulong RoomId { get; private set; }
        public string Account { get; private set; }

        // 有身份凭证即可走重连(服务端 30s 内保留会话)
        public bool CanReconnect => PlayerId != 0 && !string.IsNullOrEmpty(Token);

        public event Action<SessionState> OnStateChanged;
        public event Action<StateSnapshot> OnSnapshot;
        public event Action<BattleEnd> OnBattleEnd;
        public event Action<LeaderboardResp> OnLeaderboard;
        public event Action<string> OnError;

        public GameSession(Action<ushort, IMessage> send) => send_ = send;

        private void SetState(SessionState st)
        {
            if (State == st) return;
            State = st;
            OnStateChanged?.Invoke(st);
        }

        // —— 传输层事件(由 NetworkClient 回调驱动)——

        public void TransportConnecting() => SetState(SessionState.Connecting);

        // 连接建立:有 token 走重连,否则全新登录
        public void TransportConnected(string account)
        {
            Account = account;
            SetState(SessionState.LoggingIn);
            if (CanReconnect)
                send_(MsgIds.ReconnectReq, new ReconnectReq { PlayerId = PlayerId, SessionToken = Token });
            else
                send_(MsgIds.LoginReq, new LoginReq { Account = account });
        }

        public void TransportDisconnected()
        {
            wasInBattle_ = State == SessionState.InBattle;
            lastHeartbeatMs_ = long.MinValue;
            SetState(SessionState.Disconnected);
        }

        // —— 用户操作 ——

        // 结算后返回大厅(服务端此时会话已回 kLobby,本地状态跟上即可)
        public void BackToLobby()
        {
            if (State == SessionState.BattleEnded) SetState(SessionState.Lobby);
        }

        public void RequestMatch()
        {
            if (State != SessionState.Lobby && State != SessionState.BattleEnded) return;
            send_(MsgIds.MatchReq, new MatchReq { PlayerId = PlayerId });
            SetState(SessionState.Matching);
        }

        public void RequestLeaderboard(uint topN)
        {
            if (State == SessionState.Disconnected || State == SessionState.Connecting) return;
            send_(MsgIds.LeaderboardReq, new LeaderboardReq { TopN = topN });
        }

        // 状态式输入:摇杆姿态 + 是否攻击;仅战斗中有效
        public void SendInput(float moveX, float moveY, bool attack)
        {
            if (State != SessionState.InBattle) return;
            send_(MsgIds.PlayerInput, new PlayerInput
            {
                Seq = ++inputSeq_,
                MoveX = moveX,
                MoveY = moveY,
                Attack = attack,
            });
        }

        // 每帧调用:登录后按间隔发心跳(防 10s 踢)
        public void Tick(long nowMs)
        {
            if (State == SessionState.Disconnected || State == SessionState.Connecting
                || State == SessionState.LoggingIn) return;
            // 注意:lastHeartbeatMs_ 为初值哨兵时直接发,不能做减法(long.MinValue 相减会溢出)
            if (lastHeartbeatMs_ != long.MinValue && nowMs - lastHeartbeatMs_ < HeartbeatIntervalMs) return;
            lastHeartbeatMs_ = nowMs;
            send_(MsgIds.Heartbeat, new Heartbeat { ClientMs = nowMs });
        }

        // —— 收包分发(主线程)——

        public void HandleMessage(ushort msgId, byte[] body)
        {
            switch (msgId)
            {
                case MsgIds.LoginResp:
                    OnLoginResp(LoginResp.Parser.ParseFrom(body));
                    break;
                case MsgIds.EnterRoom:
                    OnEnterRoom(EnterRoom.Parser.ParseFrom(body));
                    break;
                case MsgIds.StateSnapshot:
                    if (State == SessionState.InBattle)
                        OnSnapshot?.Invoke(StateSnapshot.Parser.ParseFrom(body));
                    break;
                case MsgIds.BattleEnd:
                    RoomId = 0;
                    wasInBattle_ = false;
                    SetState(SessionState.BattleEnded);
                    OnBattleEnd?.Invoke(BattleEnd.Parser.ParseFrom(body));
                    break;
                case MsgIds.LeaderboardResp:
                    OnLeaderboard?.Invoke(LeaderboardResp.Parser.ParseFrom(body));
                    break;
                default:
                    break; // Echo/Heartbeat 回包等:忽略
            }
        }

        private void OnLoginResp(LoginResp resp)
        {
            if (resp.Code != 0)
            {
                // token 失效(会话过期/被顶):清凭证,下次走全新登录
                PlayerId = 0;
                Token = null;
                wasInBattle_ = false;
                OnError?.Invoke($"login/reconnect rejected: code={resp.Code}");
                return;
            }
            PlayerId = resp.PlayerId;
            Token = resp.SessionToken;
            // 重连成功且断线前在房间:直接回战场,服务端 rebind 会补全量快照
            SetState(wasInBattle_ ? SessionState.InBattle : SessionState.Lobby);
            wasInBattle_ = false;
        }

        private void OnEnterRoom(EnterRoom enter)
        {
            if (enter.RoomId == 0) // 约定:匹配超时/失败
            {
                OnError?.Invoke("match timeout");
                SetState(SessionState.Lobby);
                return;
            }
            RoomId = enter.RoomId;
            SetState(SessionState.InBattle);
        }
    }
}
