using System.Text;
using GsClient.Game;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem.UI;
using UnityEngine.UI;

namespace GsClient.Client
{
    // 全代码生成的 uGUI(零 prefab/零场景手工):
    //   左上状态栏 | 登录面板(账号+连接) | 匹配按钮 | 结算面板(胜负+排行榜+再来一局)
    // 断线重连由 GameClient 自动执行,这里只显示状态。
    public class GameUI : MonoBehaviour
    {
        private GameClient client_;
        private Text status_;
        private GameObject loginPanel_;
        private InputField accountInput_;
        private InputField serverInput_;
        private GameObject matchButton_;
        private GameObject endPanel_;
        private Text endText_;
        private string lastError_;
        private float errorUntil_; // 错误提示展示到的时刻

        private static Font font_;
        private static Font UiFont
        {
            get
            {
                if (font_ == null) font_ = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
                return font_;
            }
        }

        void Start()
        {
            client_ = GetComponent<GameClient>();
            BuildCanvas();

            var session = client_.Session;
            session.OnBattleEnd += end =>
            {
                bool win = end.WinnerId == session.PlayerId;
                endText_.text = win ? "VICTORY" : "DEFEAT";
                session.RequestLeaderboard(10); // 结算后拉排行榜
            };
            session.OnError += err =>
            {
                lastError_ = err;
                errorUntil_ = Time.realtimeSinceStartup + 3f; // 匹配超时/登录失败等,展示 3s
            };
            session.OnLeaderboard += resp =>
            {
                var sb = new StringBuilder();
                sb.AppendLine(endText_.text).AppendLine().AppendLine("-- Leaderboard --");
                int rank = 1;
                foreach (var e in resp.Entries)
                    sb.AppendLine($"{rank++}. {e.Account}  {e.Score:0}");
                endText_.text = sb.ToString();
            };
        }

        void Update()
        {
            var st = client_.Session.State;
            string err = Time.realtimeSinceStartup < errorUntil_ ? $"  [{lastError_}]" : "";
            status_.text = $"{client_.account} | {st}" +
                (st == SessionState.Disconnected && client_.Session.CanReconnect ? " (auto reconnecting...)" : "") + err;

            loginPanel_.SetActive(st == SessionState.Disconnected && !client_.Session.CanReconnect);
            matchButton_.SetActive(st == SessionState.Lobby || st == SessionState.BattleEnded);
            endPanel_.SetActive(st == SessionState.BattleEnded);
        }

        // —— UI 构建 ——

        private void BuildCanvas()
        {
            var canvasGo = new GameObject("Canvas");
            var canvas = canvasGo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = canvasGo.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1280, 720);
            canvasGo.AddComponent<GraphicRaycaster>();

            // 新 Input System 下必须用 InputSystemUIInputModule(旧 StandaloneInputModule 会报错)
            if (FindFirstObjectByType<EventSystem>() == null)
            {
                var es = new GameObject("EventSystem");
                es.AddComponent<EventSystem>();
                es.AddComponent<InputSystemUIInputModule>();
            }

            status_ = MakeText(canvasGo.transform, "Status", new Vector2(0, 1), new Vector2(10, -10),
                new Vector2(600, 30), 18, TextAnchor.UpperLeft);
            var controls = MakeText(canvasGo.transform, "Controls", new Vector2(1, 1), new Vector2(-10, -10),
                new Vector2(540, 30), 17, TextAnchor.UpperRight);
            controls.text = "WASD MOVE  |  SPACE FIRE  |  BLUE = YOU";
            controls.color = new Color(0.72f, 0.84f, 0.95f);

            // 登录面板:服务器地址(host:port,支持局域网联机)+ 账号
            loginPanel_ = MakePanel(canvasGo.transform, "LoginPanel", new Vector2(360, 180));
            MakeText(loginPanel_.transform, "Title", new Vector2(0.5f, 1), new Vector2(0, -12),
                new Vector2(320, 26), 20, TextAnchor.MiddleCenter).text = "GS Battle";
            serverInput_ = MakeInput(loginPanel_.transform, new Vector2(0, 32), new Vector2(220, 34),
                $"{client_.host}:{client_.port}");
            accountInput_ = MakeInput(loginPanel_.transform, new Vector2(0, -8), new Vector2(220, 34), "unity");
            MakeButton(loginPanel_.transform, "Connect", new Vector2(0, -56), new Vector2(140, 36), () =>
            {
                // 解析 host:port(缺端口默认 9100)
                string[] parts = serverInput_.text.Split(':');
                client_.host = parts[0].Trim();
                client_.port = parts.Length > 1 && int.TryParse(parts[1], out int p) ? p : 9100;
                client_.ConnectAs(accountInput_.text);
            });

            // 匹配按钮(屏幕下方)
            matchButton_ = MakeButton(canvasGo.transform, "Match", new Vector2(0, 60), new Vector2(160, 44),
                () => client_.RequestMatch());
            var mbRect = matchButton_.GetComponent<RectTransform>();
            mbRect.anchorMin = mbRect.anchorMax = new Vector2(0.5f, 0f);

            // 结算面板:结果+排行榜 + 返回大厅按钮(清场上残留角色)
            endPanel_ = MakePanel(canvasGo.transform, "EndPanel", new Vector2(420, 400));
            endText_ = MakeText(endPanel_.transform, "Result", new Vector2(0.5f, 1f), new Vector2(0, -16),
                new Vector2(380, 320), 20, TextAnchor.UpperCenter);
            MakeButton(endPanel_.transform, "Back to Lobby", new Vector2(0, -170), new Vector2(180, 40),
                () => client_.BackToLobby());

            loginPanel_.SetActive(false);
            matchButton_.SetActive(false);
            endPanel_.SetActive(false);
        }

        private static GameObject MakePanel(Transform parent, string name, Vector2 size)
        {
            var go = new GameObject(name);
            go.transform.SetParent(parent, false);
            var img = go.AddComponent<Image>();
            img.color = new Color(0.1f, 0.12f, 0.16f, 0.92f);
            var rect = go.GetComponent<RectTransform>();
            rect.sizeDelta = size; // 默认锚点居中
            return go;
        }

        private static Text MakeText(Transform parent, string name, Vector2 anchor, Vector2 pos,
            Vector2 size, int fontSize, TextAnchor align)
        {
            var go = new GameObject(name);
            go.transform.SetParent(parent, false);
            var text = go.AddComponent<Text>();
            text.font = UiFont;
            text.fontSize = fontSize;
            text.alignment = align;
            text.color = Color.white;
            var rect = go.GetComponent<RectTransform>();
            rect.anchorMin = rect.anchorMax = rect.pivot = anchor;
            rect.anchoredPosition = pos;
            rect.sizeDelta = size;
            return text;
        }

        private InputField MakeInput(Transform parent, Vector2 pos, Vector2 size, string defaultText)
        {
            var go = new GameObject("AccountInput");
            go.transform.SetParent(parent, false);
            var bg = go.AddComponent<Image>();
            bg.color = new Color(0.2f, 0.22f, 0.28f);
            var rect = go.GetComponent<RectTransform>();
            rect.anchoredPosition = pos;
            rect.sizeDelta = size;

            var textGo = MakeText(go.transform, "Text", new Vector2(0.5f, 0.5f), Vector2.zero,
                size - new Vector2(16, 6), 18, TextAnchor.MiddleLeft);
            var input = go.AddComponent<InputField>();
            input.textComponent = textGo;
            input.text = defaultText;
            return input;
        }

        private static GameObject MakeButton(Transform parent, string label, Vector2 pos, Vector2 size,
            UnityEngine.Events.UnityAction onClick)
        {
            var go = new GameObject($"Btn_{label}");
            go.transform.SetParent(parent, false);
            var img = go.AddComponent<Image>();
            img.color = new Color(0.25f, 0.5f, 0.9f);
            var btn = go.AddComponent<Button>();
            btn.onClick.AddListener(onClick);
            var rect = go.GetComponent<RectTransform>();
            rect.anchoredPosition = pos;
            rect.sizeDelta = size;
            MakeText(go.transform, "Label", new Vector2(0.5f, 0.5f), Vector2.zero, size, 18,
                TextAnchor.MiddleCenter).text = label;
            return go;
        }
    }
}
