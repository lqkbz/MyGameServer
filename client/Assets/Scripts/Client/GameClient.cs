using System.Collections.Generic;
using GsClient.Game;
using GsClient.Net;
using UnityEngine;
using UnityEngine.InputSystem;

namespace GsClient.Client
{
    // 客户端总装(场景里唯一需要手挂的组件):
    //   NetworkClient(传输线程) → GameSession(状态机) → SnapshotInterpolator(渲染时间) → PlayerView
    // 服务端权威:本类只发"意图"(摇杆姿态/攻击),位置血量全信服务器快照。
    public class GameClient : MonoBehaviour
    {
        [Header("连接")]
        public string host = "127.0.0.1";
        public int port = 9100;
        public string account = "unity";
        [Header("自动化开关(UI 演示时关闭,自动化冒烟时打开)")]
        public bool autoConnect = true;
        public bool autoMatch = true;

        [Header("断线重连")]
        public bool autoReconnect = true;
        [Tooltip("调试:置 true 模拟拔线(Inspector/REST 可切)")]
        public bool debugDrop;
        private float reconnectAt_ = -1f; // 断线后 1s 自动重连的时间戳

        // 服务器场地 100×100 → 世界坐标 10×10
        public const float WorldScale = 0.1f;
        private const float InputIntervalSec = 0.1f; // 状态式输入:每 100ms 上报姿态

        private NetworkClient net_;
        private GameSession session_;
        private SnapshotInterpolator interp_;
        private readonly Dictionary<ulong, PlayerView> views_ = new Dictionary<ulong, PlayerView>();
        private readonly Dictionary<ulong, ProjectileView> projectileViews_ =
            new Dictionary<ulong, ProjectileView>();
        private float latestSnapAt_;    // 收到最新快照的本地时刻
        private float lastInputSentAt_;
        private bool attackLatch_;      // 两次上报之间按过攻击键则置位

        public GameSession Session => session_;

        void Awake()
        {
            // 网络客户端必须后台运行:编辑器/窗口失焦时 Update 停跑会断心跳被服务器踢
            Application.runInBackground = true;

            net_ = new NetworkClient();
            session_ = new GameSession((id, msg) => net_.Send(id, msg));
            interp_ = new SnapshotInterpolator(delayTicks: 2, tickMs: 50);

            // 传输事件 → 状态机(都在主线程 Pump 里回调,无并发问题)
            net_.OnConnected += () => session_.TransportConnected(account);
            net_.OnDisconnected += reason =>
            {
                Debug.Log($"[net] disconnected: {reason}");
                session_.TransportDisconnected();
            };
            net_.OnMessage += (id, body) => session_.HandleMessage(id, body);

            session_.OnStateChanged += st =>
            {
                Debug.Log($"[session] -> {st}");
                if (st == SessionState.InBattle) ClearViews(); // 新一局:清掉上一局残留角色
                if (st == SessionState.Lobby && autoMatch) session_.RequestMatch();
            };
            session_.OnSnapshot += snap =>
            {
                interp_.Push(snap);
                latestSnapAt_ = Time.realtimeSinceStartup;
            };
            session_.OnBattleEnd += end =>
                Debug.Log($"[battle] end winner={end.WinnerId} " +
                          (end.WinnerId == session_.PlayerId ? "WIN" : "LOSE"));
            session_.OnError += err => Debug.LogWarning($"[session] {err}");

            BuildArena();
            gameObject.AddComponent<GameUI>(); // UI 也全代码生成,场景零手工
        }

        void Start()
        {
            if (autoConnect) Connect();
        }

        public void Connect()
        {
            session_.TransportConnecting();
            net_.Connect(host, port);
        }

        // UI 入口:以指定账号连接
        public void ConnectAs(string acc)
        {
            if (!string.IsNullOrEmpty(acc)) account = acc;
            Connect();
        }

        public void RequestMatch() => session_.RequestMatch();

        // 结算后返回大厅:清场上角色 + 状态机回 Lobby
        public void BackToLobby()
        {
            ClearViews();
            session_.BackToLobby();
        }

        // 销毁全部玩家视图(重连回战场时插值器 full 快照会重建)
        private void ClearViews()
        {
            foreach (var kv in views_)
                if (kv.Value != null) Destroy(kv.Value.gameObject);
            views_.Clear();
            foreach (var kv in projectileViews_)
                if (kv.Value != null) Destroy(kv.Value.gameObject);
            projectileViews_.Clear();
            interp_ = new SnapshotInterpolator(delayTicks: 2, tickMs: 50);
        }

        void Update()
        {
            // 调试断线:模拟拔线,走 token 重连链路
            if (debugDrop)
            {
                debugDrop = false;
                Debug.Log("[debug] force drop");
                net_.Close();
                session_.TransportDisconnected();
            }
            // 断线自动重连:有 token 时 1s 后用 ReconnectReq 回战场
            if (autoReconnect && session_.State == SessionState.Disconnected && session_.CanReconnect)
            {
                if (reconnectAt_ < 0f) reconnectAt_ = Time.realtimeSinceStartup + 1f;
                else if (Time.realtimeSinceStartup >= reconnectAt_)
                {
                    reconnectAt_ = -1f;
                    Debug.Log("[net] reconnecting...");
                    Connect();
                }
            }
            else reconnectAt_ = -1f;

            net_.Pump();                                                  // 网络事件回主线程
            session_.Tick((long)(Time.realtimeSinceStartup * 1000f));     // 心跳
            SampleInput();
            RenderPlayers();
            RenderProjectiles();
        }

        void OnDestroy() => net_?.Close();

        // —— 输入:WASD 移动 + 空格攻击,每 100ms 状态式上报 ——
        private void SampleInput()
        {
            var kb = Keyboard.current;
            if (kb == null) return;
            if (kb.spaceKey.wasPressedThisFrame) attackLatch_ = true; // 锁存,避免 100ms 间隔漏按

            if (session_.State != SessionState.InBattle) return;
            if (Time.realtimeSinceStartup - lastInputSentAt_ < InputIntervalSec) return;
            lastInputSentAt_ = Time.realtimeSinceStartup;

            float mx = (kb.dKey.isPressed ? 1f : 0f) - (kb.aKey.isPressed ? 1f : 0f);
            float my = (kb.wKey.isPressed ? 1f : 0f) - (kb.sKey.isPressed ? 1f : 0f);
            if (mx != 0f && my != 0f) { mx *= 0.7071f; my *= 0.7071f; } // 斜向归一化

            session_.SendInput(mx, my, attackLatch_);
            attackLatch_ = false;
        }

        // —— 渲染:按"最新快照-2tick"的延迟时间采样插值 ——
        private void RenderPlayers()
        {
            if (!interp_.HasData) return;
            float elapsedMs = (Time.realtimeSinceStartup - latestSnapAt_) * 1000f;
            float renderTick = interp_.CurrentRenderTick(elapsedMs);

            foreach (ulong pid in interp_.Players)
            {
                if (!interp_.TrySample(pid, renderTick, out SampledState st)) continue;
                if (!views_.TryGetValue(pid, out PlayerView view))
                {
                    view = PlayerView.Create(pid, pid == session_.PlayerId);
                    views_[pid] = view;
                }
                view.Apply(new Vector2(st.X * WorldScale, st.Y * WorldScale), st.Hp);
            }
        }

        private void RenderProjectiles()
        {
            var active = new HashSet<ulong>();
            foreach (ulong projectileId in interp_.Projectiles)
            {
                if (!interp_.TryGetProjectile(projectileId, out SampledProjectile projectile)) continue;
                active.Add(projectileId);
                if (!projectileViews_.TryGetValue(projectileId, out ProjectileView view))
                {
                    view = ProjectileView.Create(
                        projectileId, projectile.OwnerId == session_.PlayerId);
                    projectileViews_[projectileId] = view;
                }
                view.Apply(
                    new Vector2(projectile.X * WorldScale, projectile.Y * WorldScale),
                    new Vector2(projectile.VelocityX, projectile.VelocityY));
            }

            var removed = new List<ulong>();
            foreach (var kv in projectileViews_)
                if (!active.Contains(kv.Key)) removed.Add(kv.Key);
            foreach (ulong projectileId in removed)
            {
                ProjectileView view = projectileViews_[projectileId];
                projectileViews_.Remove(projectileId);
                if (view != null) view.ExplodeAndDestroy();
            }
        }

        // —— 场地:100×100 边框 + 相机取景(全部代码生成,场景零手工)——
        private void BuildArena()
        {
            var cam = Camera.main;
            if (cam != null)
            {
                cam.transform.position = new Vector3(5f, 5f, -10f);
                cam.orthographic = true;
                cam.orthographicSize = 6f;
                cam.clearFlags = CameraClearFlags.SolidColor; // 2D 场景不用天空盒
                cam.backgroundColor = new Color(0.08f, 0.09f, 0.12f);
            }

            var arena = new GameObject("Arena");

            // 草地地板:一个 Tiled 模式 SpriteRenderer 平铺整场(缺素材则跳过,保持深色背景)
            Sprite floorTile = Resources.Load<Sprite>("Art/floor_tile");
            if (floorTile != null)
            {
                var floor = new GameObject("floor");
                floor.transform.SetParent(arena.transform, false);
                var fsr = floor.AddComponent<SpriteRenderer>();
                fsr.sprite = floorTile;
                fsr.drawMode = SpriteDrawMode.Tiled;
                fsr.size = new Vector2(10f, 10f);
                fsr.sortingOrder = -10;
                floor.transform.position = new Vector3(5f, 5f, 0f);
            }
            void Wall(string name, Vector2 pos, Vector2 scale)
            {
                var go = new GameObject(name);
                go.transform.SetParent(arena.transform, false);
                var sr = go.AddComponent<SpriteRenderer>();
                sr.sprite = PlayerView.WhiteSprite;
                sr.color = new Color(0.4f, 0.42f, 0.5f);
                go.transform.position = pos;
                go.transform.localScale = new Vector3(scale.x, scale.y, 1f);
            }
            const float w = 10f, t = 0.08f; // 世界尺寸/边框厚度
            Wall("bottom", new Vector2(w / 2, 0f), new Vector2(w + t, t));
            Wall("top", new Vector2(w / 2, w), new Vector2(w + t, t));
            Wall("left", new Vector2(0f, w / 2), new Vector2(t, w + t));
            Wall("right", new Vector2(w, w / 2), new Vector2(t, w + t));
        }
    }
}
