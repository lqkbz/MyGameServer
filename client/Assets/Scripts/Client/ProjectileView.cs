using UnityEngine;

namespace GsClient.Client
{
    // 服务器权威子弹的纯代码视图：发光弹体 + TrailRenderer 拖尾 + 销毁闪光。
    // 不依赖 prefab/贴图，方便协议联调和双客户端同步演示。
    public sealed class ProjectileView : MonoBehaviour
    {
        private SpriteRenderer core_;
        private SpriteRenderer glow_;
        private TrailRenderer trail_;
        private Vector2 targetPosition_;
        private Color color_;
        private bool initialized_;
        private bool exploding_;

        private static Material trailMaterial_;
        private static Material TrailMaterial
        {
            get
            {
                if (trailMaterial_ == null)
                {
                    trailMaterial_ = new Material(Shader.Find("Sprites/Default"));
                    trailMaterial_.name = "ProjectileTrail_Runtime";
                }
                return trailMaterial_;
            }
        }

        public static ProjectileView Create(ulong projectileId, bool isLocal)
        {
            var root = new GameObject($"Projectile_{projectileId}");
            var view = root.AddComponent<ProjectileView>();
            view.color_ = isLocal
                ? new Color(0.15f, 0.9f, 1f, 1f)
                : new Color(1f, 0.48f, 0.16f, 1f);

            view.glow_ = MakeSprite(root.transform, "glow",
                new Color(view.color_.r, view.color_.g, view.color_.b, 0.28f), 18);
            view.glow_.transform.localScale = new Vector3(0.46f, 0.24f, 1f);

            view.core_ = MakeSprite(root.transform, "core", Color.white, 20);
            view.core_.transform.localScale = new Vector3(0.25f, 0.11f, 1f);

            view.trail_ = root.AddComponent<TrailRenderer>();
            view.trail_.sharedMaterial = TrailMaterial;
            view.trail_.time = 0.22f;
            view.trail_.startWidth = 0.14f;
            view.trail_.endWidth = 0.015f;
            view.trail_.startColor = view.color_;
            view.trail_.endColor = new Color(view.color_.r, view.color_.g, view.color_.b, 0f);
            view.trail_.sortingOrder = 17;
            view.trail_.minVertexDistance = 0.025f;
            view.trail_.numCornerVertices = 2;
            return view;
        }

        private static SpriteRenderer MakeSprite(Transform parent, string name, Color color, int order)
        {
            var go = new GameObject(name);
            go.transform.SetParent(parent, false);
            var renderer = go.AddComponent<SpriteRenderer>();
            renderer.sprite = PlayerView.WhiteSprite;
            renderer.color = color;
            renderer.sortingOrder = order;
            return renderer;
        }

        public void Apply(Vector2 worldPosition, Vector2 velocity)
        {
            targetPosition_ = worldPosition;
            if (!initialized_)
            {
                transform.position = worldPosition;
                initialized_ = true;
            }
            if (velocity.sqrMagnitude > 1e-5f)
                transform.rotation = Quaternion.Euler(
                    0f, 0f, Mathf.Atan2(velocity.y, velocity.x) * Mathf.Rad2Deg);
        }

        private void Update()
        {
            if (!initialized_ || exploding_) return;
            transform.position = Vector2.Lerp(
                transform.position, targetPosition_, 1f - Mathf.Exp(-28f * Time.deltaTime));
            float pulse = 1f + Mathf.Sin(Time.time * 24f) * 0.12f;
            glow_.transform.localScale = new Vector3(0.46f * pulse, 0.24f * pulse, 1f);
        }

        public void ExplodeAndDestroy()
        {
            if (exploding_) return;
            exploding_ = true;
            core_.enabled = false;
            glow_.enabled = false;
            trail_.emitting = false;
            ImpactPulse.Create(transform.position, color_);
            Destroy(gameObject, trail_.time + 0.04f);
        }

        private sealed class ImpactPulse : MonoBehaviour
        {
            private SpriteRenderer outer_;
            private SpriteRenderer inner_;
            private Color color_;
            private float age_;
            private const float Duration = 0.24f;

            public static void Create(Vector3 position, Color color)
            {
                var root = new GameObject("ProjectileImpact");
                root.transform.position = position;
                var pulse = root.AddComponent<ImpactPulse>();
                pulse.color_ = color;
                pulse.outer_ = MakeSprite(root.transform, "outer",
                    new Color(color.r, color.g, color.b, 0.75f), 22);
                pulse.outer_.transform.rotation = Quaternion.Euler(0f, 0f, 45f);
                pulse.inner_ = MakeSprite(root.transform, "inner", Color.white, 23);
                pulse.outer_.transform.localScale = new Vector3(0.18f, 0.18f, 1f);
                pulse.inner_.transform.localScale = new Vector3(0.13f, 0.13f, 1f);
            }

            private void Update()
            {
                age_ += Time.deltaTime;
                float t = Mathf.Clamp01(age_ / Duration);
                float eased = 1f - (1f - t) * (1f - t);
                outer_.transform.localScale = Vector3.one * Mathf.Lerp(0.18f, 0.9f, eased);
                inner_.transform.localScale = Vector3.one * Mathf.Lerp(0.13f, 0.35f, eased);
                outer_.color = new Color(color_.r, color_.g, color_.b, 0.75f * (1f - t));
                inner_.color = new Color(1f, 1f, 1f, 1f - t);
                if (age_ >= Duration) Destroy(gameObject);
            }
        }
    }
}
