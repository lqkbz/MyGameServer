using UnityEngine;

namespace GsClient.Client
{
    // 玩家渲染视图:占位用色块方块 + 头顶血条(T8 换皮时替换 sprite 即可)
    public class PlayerView : MonoBehaviour
    {
        private SpriteRenderer body_;
        private Transform hpFill_;
        private const float HpBarWidth = 0.8f;

        private static Sprite whiteSprite_;

        // 运行时生成 1×1 白色 sprite,免素材依赖
        public static Sprite WhiteSprite
        {
            get
            {
                if (whiteSprite_ == null)
                {
                    Texture2D tex = Texture2D.whiteTexture;
                    whiteSprite_ = Sprite.Create(
                        tex, new Rect(0, 0, tex.width, tex.height),
                        new Vector2(0.5f, 0.5f), tex.width);
                }
                return whiteSprite_;
            }
        }

        private static SpriteRenderer MakeQuad(Transform parent, string name, Color color, int order)
        {
            var go = new GameObject(name);
            go.transform.SetParent(parent, false);
            var sr = go.AddComponent<SpriteRenderer>();
            sr.sprite = WhiteSprite;
            sr.color = color;
            sr.sortingOrder = order;
            return sr;
        }

        public static PlayerView Create(ulong playerId, bool isLocal)
        {
            var root = new GameObject($"Player_{playerId}");
            var view = root.AddComponent<PlayerView>();

            // 优先用 Kenney 素材(Resources/Art),缺素材时回退纯色方块
            Sprite skin = Resources.Load<Sprite>(isLocal ? "Art/player_local" : "Art/player_remote");
            if (skin != null)
            {
                view.body_ = MakeQuad(root.transform, "body", Color.white, 10);
                view.body_.sprite = skin; // 49×43px @PPU100 ≈ 0.5 世界单位,天然合适
                view.body_.transform.localScale = Vector3.one;
            }
            else
            {
                // 回退:本地=青,对手=橙红
                view.body_ = MakeQuad(root.transform, "body",
                    isLocal ? new Color(0.2f, 0.85f, 0.9f) : new Color(0.95f, 0.45f, 0.3f), 10);
                view.body_.transform.localScale = new Vector3(0.5f, 0.5f, 1f);
            }

            // 血条:背景 + 左对齐填充
            var bg = MakeQuad(root.transform, "hp_bg", new Color(0.15f, 0.15f, 0.15f), 11);
            bg.transform.localPosition = new Vector3(0f, 0.45f, 0f);
            bg.transform.localScale = new Vector3(HpBarWidth, 0.09f, 1f);
            var fill = MakeQuad(root.transform, "hp_fill", new Color(0.25f, 0.9f, 0.3f), 12);
            fill.transform.localPosition = new Vector3(0f, 0.45f, 0f);
            fill.transform.localScale = new Vector3(HpBarWidth, 0.07f, 1f);
            view.hpFill_ = fill.transform;
            return view;
        }

        private Vector2 lastPos_;

        public void Apply(Vector2 worldPos, int hp)
        {
            // 朝向:按移动方向旋转本体(Kenney 人物默认朝右);血条不随身体转
            Vector2 delta = worldPos - lastPos_;
            if (delta.sqrMagnitude > 1e-6f)
                body_.transform.rotation = Quaternion.Euler(0, 0,
                    Mathf.Atan2(delta.y, delta.x) * Mathf.Rad2Deg);
            lastPos_ = worldPos;
            transform.position = worldPos;
            float frac = Mathf.Clamp01(hp / 100f);
            // 左对齐收缩:填充条中心随宽度左移
            hpFill_.localScale = new Vector3(HpBarWidth * frac, 0.07f, 1f);
            hpFill_.localPosition = new Vector3(-HpBarWidth * (1f - frac) * 0.5f, 0.45f, 0f);
            // 残血变色
            var sr = hpFill_.GetComponent<SpriteRenderer>();
            sr.color = frac > 0.5f ? new Color(0.25f, 0.9f, 0.3f)
                     : frac > 0.25f ? new Color(0.95f, 0.8f, 0.2f)
                                    : new Color(0.95f, 0.25f, 0.2f);
        }
    }
}
