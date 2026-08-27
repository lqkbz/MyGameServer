#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gs::timer {

// 两级时间轮:L0 精度 100ms × 64 槽(量程 6.4s),
// L1 每槽 6.4s × 64 槽(量程 ~409.6s,覆盖本项目最长 30s 的 session 保留)。
// 到期时间落在 L0 量程内的直接挂 L0;更远的挂 L1,L1 槽到期时把
// 其中的定时器"级联"降级重新挂到 L0——这是层级时间轮 O(1) 的关键:
// 每个定时器至多被搬运 O(层数) 次,而不是每 tick 都要比较。
// 纯逻辑类:不感知真实时间,由外部(timerfd)调用 advance 推进。
class TimerWheel {
 public:
  using Callback = std::function<void()>;

  static constexpr uint64_t kTickMs = 100;
  static constexpr size_t kSlots = 64;  // 每级槽数

  // 返回定时器 id(从 1 递增),0 保留为非法值
  uint64_t add(uint64_t delayMs, Callback cb);
  void cancel(uint64_t id);
  // 推进 elapsedMs;不足一个 tick 的余量内部累积
  void advance(uint64_t elapsedMs);

 private:
  struct Node {
    uint64_t id;
    uint64_t expiryTick;  // 绝对 tick
    Callback cb;
    bool canceled = false;
  };
  using NodePtr = std::shared_ptr<Node>;

  void place(const NodePtr& node);  // 按剩余距离挂 L0 或 L1
  void fireSlot(std::vector<NodePtr>& slot);

  uint64_t nowTick_ = 0;
  uint64_t remainderMs_ = 0;
  uint64_t nextId_ = 1;
  std::vector<NodePtr> l0_[kSlots];
  std::vector<NodePtr> l1_[kSlots];
  std::unordered_map<uint64_t, NodePtr> alive_;  // id → node,供 cancel
};

}  // namespace gs::timer
