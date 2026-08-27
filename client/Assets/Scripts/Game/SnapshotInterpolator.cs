using System.Collections.Generic;
using Gs.Pb;

namespace GsClient.Game
{
    // 采样结果(插值后的玩家状态)
    public struct SampledState
    {
        public float X;
        public float Y;
        public int Hp;
        public uint LastInputSeq;
    }

    // 子弹由服务器逐 tick 给出权威位置；视图层再做轻量平滑和拖尾。
    public struct SampledProjectile
    {
        public ulong OwnerId;
        public float X, Y;
        public float VelocityX, VelocityY;
    }

    // 快照插值器(纯 C#):把 20Hz 的服务器快照变成任意渲染时刻的平滑状态。
    //
    // 原理:渲染时间故意落后最新快照 delayTicks 个 tick(默认 2 tick=100ms),
    // 使当前渲染时刻两侧总有关键帧可用——网络抖动被缓冲吸收,画面不跳变。
    // 只内插不外推:宁可画面滞后,不做会被服务器"打脸"的预测。
    //
    // 增量快照语义:未出现的玩家=没变化。Push 时把这些玩家的旧状态
    // 前滚为当前 tick 的关键帧,保证轨迹按"静止→再动"正确插值,
    // 而不是在两个远距离关键帧之间慢速漂移。
    public class SnapshotInterpolator
    {
        private struct Keyframe
        {
            public uint Tick;
            public float X, Y;
            public int Hp;
            public uint LastInputSeq;
        }

        private const int kMaxKeyframes = 64; // 每玩家保留窗口(64 tick≈3.2s)

        private readonly int delayTicks_;
        private readonly int tickMs_;
        private readonly Dictionary<ulong, List<Keyframe>> tracks_ = new Dictionary<ulong, List<Keyframe>>();
        private readonly Dictionary<ulong, SampledProjectile> projectiles_ =
            new Dictionary<ulong, SampledProjectile>();

        public uint LatestTick { get; private set; }
        public bool HasData => LatestTick != 0;

        public SnapshotInterpolator(int delayTicks = 2, int tickMs = 50)
        {
            delayTicks_ = delayTicks;
            tickMs_ = tickMs;
        }

        // 收到最新快照 elapsedMs 毫秒后,应渲染的逻辑时刻(小数 tick)
        public float CurrentRenderTick(float elapsedMsSinceLatest)
            => LatestTick - delayTicks_ + elapsedMsSinceLatest / tickMs_;

        public void Push(StateSnapshot snap)
        {
            if (snap.Full)
            {
                // 全量(进房/重连/tick 跳变):推倒重建
                tracks_.Clear();
                projectiles_.Clear();
                LatestTick = 0;
            }
            if (LatestTick != 0 && snap.Tick <= LatestTick) return; // 乱序旧包:丢弃

            // 1) 快照里出现的玩家:追加新关键帧(新玩家建轨迹)
            foreach (PlayerState p in snap.Players)
            {
                if (!tracks_.TryGetValue(p.PlayerId, out var track))
                {
                    track = new List<Keyframe>();
                    tracks_[p.PlayerId] = track;
                }
                AppendKeyframe(track, new Keyframe
                {
                    Tick = snap.Tick, X = p.X, Y = p.Y, Hp = p.Hp, LastInputSeq = p.LastInputSeq,
                });
            }
            // 2) 未出现的玩家=没变化:旧状态前滚为本 tick 关键帧
            foreach (var kv in tracks_)
            {
                var track = kv.Value;
                if (track.Count == 0 || track[track.Count - 1].Tick == snap.Tick) continue;
                Keyframe carry = track[track.Count - 1];
                carry.Tick = snap.Tick;
                AppendKeyframe(track, carry);
            }

            // 子弹快照包含当前全部存活子弹；销毁 ID 是增量 tombstone。
            foreach (ProjectileState projectile in snap.Projectiles)
            {
                projectiles_[projectile.ProjectileId] = new SampledProjectile
                {
                    OwnerId = projectile.OwnerId,
                    X = projectile.X,
                    Y = projectile.Y,
                    VelocityX = projectile.VelocityX,
                    VelocityY = projectile.VelocityY,
                };
            }
            foreach (ulong projectileId in snap.RemovedProjectileIds)
                projectiles_.Remove(projectileId);
            LatestTick = snap.Tick;
        }

        private static void AppendKeyframe(List<Keyframe> track, Keyframe kf)
        {
            track.Add(kf);
            if (track.Count > kMaxKeyframes) track.RemoveAt(0);
        }

        // 在 renderTick 时刻采样玩家状态;只内插/钳制,不外推
        public bool TrySample(ulong playerId, float renderTick, out SampledState state)
        {
            state = default;
            if (!tracks_.TryGetValue(playerId, out var track) || track.Count == 0) return false;

            // renderTick 在窗口两端:钳到边界关键帧
            if (renderTick <= track[0].Tick) { state = ToState(track[0]); return true; }
            Keyframe last = track[track.Count - 1];
            if (renderTick >= last.Tick) { state = ToState(last); return true; }

            // 二分找右邻关键帧 b(第一个 tick > renderTick),a 为其左邻
            int lo = 0, hi = track.Count - 1;
            while (lo < hi)
            {
                int mid = (lo + hi) / 2;
                if (track[mid].Tick > renderTick) hi = mid;
                else lo = mid + 1;
            }
            Keyframe b = track[lo];
            Keyframe a = track[lo - 1];
            float t = (renderTick - a.Tick) / (b.Tick - a.Tick);
            state = new SampledState
            {
                X = a.X + (b.X - a.X) * t,
                Y = a.Y + (b.Y - a.Y) * t,
                Hp = a.Hp,                    // 离散量不插值:取已确认状态
                LastInputSeq = a.LastInputSeq,
            };
            return true;
        }

        private static SampledState ToState(Keyframe kf)
            => new SampledState { X = kf.X, Y = kf.Y, Hp = kf.Hp, LastInputSeq = kf.LastInputSeq };

        // 当前有轨迹的玩家(渲染层遍历刷新用)
        public IEnumerable<ulong> Players => tracks_.Keys;

        public IEnumerable<ulong> Projectiles => projectiles_.Keys;

        public bool TryGetProjectile(ulong projectileId, out SampledProjectile projectile)
            => projectiles_.TryGetValue(projectileId, out projectile);
    }
}
