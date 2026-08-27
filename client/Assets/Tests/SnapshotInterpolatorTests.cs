using Gs.Pb;
using GsClient.Game;
using NUnit.Framework;

namespace GsClient.Tests
{
    // SnapshotInterpolator 单测:20Hz 快照 → 任意渲染时刻的平滑位置
    // 关键语义:
    // - 渲染时间 = 最新快照 tick - 延迟(2 个 tick=100ms),躲在缓冲后面吃网络抖动
    // - 增量快照只带变化玩家,插值器须与上一状态合并
    // - full=true(进房/重连)清空重建
    public class SnapshotInterpolatorTests
    {
        private static StateSnapshot Snap(uint tick, bool full, params (ulong id, float x, float y, int hp)[] ps)
        {
            var s = new StateSnapshot { Tick = tick, Full = full };
            foreach (var p in ps)
                s.Players.Add(new PlayerState { PlayerId = p.id, X = p.x, Y = p.y, Hp = p.hp });
            return s;
        }

        [Test]
        public void Interpolates_Midpoint_BetweenTicks()
        {
            var it = new SnapshotInterpolator(delayTicks: 2, tickMs: 50);
            it.Push(Snap(10, true, (7UL, 0f, 0f, 100)));
            it.Push(Snap(11, false, (7UL, 10f, 0f, 100)));
            it.Push(Snap(12, false, (7UL, 20f, 0f, 100)));

            // 渲染时间 = 12 - 2 = tick10;半个 tick 后应在 10~11 中点
            Assert.IsTrue(it.TrySample(7UL, renderTick: 10.5f, out var st));
            Assert.AreEqual(5f, st.X, 1e-3);
            Assert.AreEqual(0f, st.Y, 1e-3);
        }

        [Test]
        public void RenderTick_LagsLatestByDelay()
        {
            var it = new SnapshotInterpolator(delayTicks: 2, tickMs: 50);
            it.Push(Snap(10, true, (7UL, 0f, 0f, 100)));
            it.Push(Snap(11, false, (7UL, 10f, 0f, 100)));
            it.Push(Snap(12, false, (7UL, 20f, 0f, 100)));
            // 刚收到 tick12 时渲染时间应回退 2 tick
            Assert.AreEqual(10f, it.CurrentRenderTick(elapsedMsSinceLatest: 0), 1e-3);
            // 又过 50ms → 前进 1 tick
            Assert.AreEqual(11f, it.CurrentRenderTick(elapsedMsSinceLatest: 50), 1e-3);
        }

        [Test]
        public void Incremental_MergesUnchangedPlayers()
        {
            var it = new SnapshotInterpolator(delayTicks: 1, tickMs: 50);
            it.Push(Snap(10, true, (7UL, 0f, 0f, 100), (8UL, 50f, 50f, 100)));
            // tick11 只有 7 动了;8 未出现 → 沿用旧状态
            it.Push(Snap(11, false, (7UL, 10f, 0f, 100)));

            Assert.IsTrue(it.TrySample(8UL, renderTick: 10.5f, out var st8));
            Assert.AreEqual(50f, st8.X, 1e-3); // 8 没动,插值结果不变
            Assert.IsTrue(it.TrySample(7UL, renderTick: 10.5f, out var st7));
            Assert.AreEqual(5f, st7.X, 1e-3);
        }

        [Test]
        public void FullSnapshot_ResetsState()
        {
            var it = new SnapshotInterpolator(delayTicks: 1, tickMs: 50);
            it.Push(Snap(10, true, (7UL, 0f, 0f, 100), (8UL, 50f, 50f, 100)));
            // 重连:服务端发全量,tick 跳变,且 8 已不在(理论场景)
            it.Push(Snap(100, true, (7UL, 30f, 30f, 40)));

            Assert.IsFalse(it.TrySample(8UL, renderTick: 100f, out _), "full 后旧玩家应被清掉");
            Assert.IsTrue(it.TrySample(7UL, renderTick: 100f, out var st));
            Assert.AreEqual(30f, st.X, 1e-3);
            Assert.AreEqual(40, st.Hp);
        }

        [Test]
        public void OutOfOrder_OldTick_Dropped()
        {
            var it = new SnapshotInterpolator(delayTicks: 1, tickMs: 50);
            it.Push(Snap(10, true, (7UL, 0f, 0f, 100)));
            it.Push(Snap(12, false, (7UL, 20f, 0f, 100)));
            it.Push(Snap(11, false, (7UL, 999f, 0f, 100))); // 乱序旧包:丢弃

            Assert.AreEqual(12u, it.LatestTick);
            // tick11.0 采样应在 10→12 之间线性(999 不参与)
            Assert.IsTrue(it.TrySample(7UL, renderTick: 11f, out var st));
            Assert.AreEqual(10f, st.X, 1e-3);
        }

        [Test]
        public void SampleBeyondLatest_ClampsToLatest()
        {
            var it = new SnapshotInterpolator(delayTicks: 1, tickMs: 50);
            it.Push(Snap(10, true, (7UL, 0f, 0f, 100)));
            it.Push(Snap(11, false, (7UL, 10f, 0f, 100)));
            // 渲染时间越过最新快照(网络卡顿):钳到最新,不外推
            Assert.IsTrue(it.TrySample(7UL, renderTick: 13f, out var st));
            Assert.AreEqual(10f, st.X, 1e-3);
        }

        [Test]
        public void ProjectileState_UpdatesAndRemovalTombstoneDeletesIt()
        {
            var it = new SnapshotInterpolator(delayTicks: 1, tickMs: 50);
            var spawn = Snap(10, true, (7UL, 0f, 0f, 100));
            spawn.Projectiles.Add(new ProjectileState
            {
                ProjectileId = 101,
                OwnerId = 7,
                X = 10,
                Y = 20,
                VelocityX = 60,
            });
            it.Push(spawn);

            Assert.IsTrue(it.TryGetProjectile(101, out var projectile));
            Assert.AreEqual(7UL, projectile.OwnerId);
            Assert.AreEqual(10f, projectile.X, 1e-3);

            var moved = Snap(11, false);
            moved.Projectiles.Add(new ProjectileState
            {
                ProjectileId = 101,
                OwnerId = 7,
                X = 13,
                Y = 20,
                VelocityX = 60,
            });
            it.Push(moved);
            Assert.IsTrue(it.TryGetProjectile(101, out projectile));
            Assert.AreEqual(13f, projectile.X, 1e-3);

            var removed = Snap(12, false);
            removed.RemovedProjectileIds.Add(101);
            it.Push(removed);
            Assert.IsFalse(it.TryGetProjectile(101, out _));
        }

        [Test]
        public void FullSnapshot_ClearsOldProjectiles()
        {
            var it = new SnapshotInterpolator(delayTicks: 1, tickMs: 50);
            var first = Snap(10, true, (7UL, 0f, 0f, 100));
            first.Projectiles.Add(new ProjectileState { ProjectileId = 5, OwnerId = 7 });
            it.Push(first);
            Assert.IsTrue(it.TryGetProjectile(5, out _));

            it.Push(Snap(100, true, (7UL, 30f, 30f, 100)));
            Assert.IsFalse(it.TryGetProjectile(5, out _));
        }
    }
}
