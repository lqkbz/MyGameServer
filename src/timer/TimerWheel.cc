#include "timer/TimerWheel.h"

namespace gs::timer {

uint64_t TimerWheel::add(uint64_t delayMs, Callback cb) {
  // 至少 1 tick:0 延迟也要等到下一次步进,保证"回调不在 add 调用栈里执行"
  uint64_t ticks = (delayMs + kTickMs - 1) / kTickMs;
  if (ticks == 0) ticks = 1;
  auto node = std::make_shared<Node>();
  node->id = nextId_++;
  node->expiryTick = nowTick_ + ticks;
  node->cb = std::move(cb);
  alive_[node->id] = node;
  place(node);
  return node->id;
}

void TimerWheel::cancel(uint64_t id) {
  auto it = alive_.find(id);
  if (it == alive_.end()) return;
  it->second->canceled = true;  // 惰性删除:槽里的引用等到期时跳过
  alive_.erase(it);
}

void TimerWheel::place(const NodePtr& node) {
  uint64_t diff = node->expiryTick - nowTick_;
  if (diff < kSlots) {
    l0_[node->expiryTick % kSlots].push_back(node);
  } else {
    // 超出 L1 量程的截断到最远槽,级联时会再按剩余距离重挂
    if (diff >= kSlots * kSlots) {
      node->expiryTick = nowTick_ + kSlots * kSlots - 1;
    }
    l1_[(node->expiryTick / kSlots) % kSlots].push_back(node);
  }
}

void TimerWheel::fireSlot(std::vector<NodePtr>& slot) {
  std::vector<NodePtr> pending;
  pending.swap(slot);  // 先取出:回调里可能再 add 到同一槽
  for (auto& n : pending) {
    if (n->canceled) continue;
    alive_.erase(n->id);
    n->cb();
  }
}

void TimerWheel::advance(uint64_t elapsedMs) {
  remainderMs_ += elapsedMs;
  uint64_t steps = remainderMs_ / kTickMs;
  remainderMs_ %= kTickMs;
  for (uint64_t s = 0; s < steps; ++s) {
    ++nowTick_;
    // L0 走满一圈时,先把 L1 当前槽级联下来(其中 expiry==nowTick_ 的
    // 会落在马上要触发的 L0 槽里,不会被漏掉)
    if (nowTick_ % kSlots == 0) {
      std::vector<NodePtr> cascade;
      cascade.swap(l1_[(nowTick_ / kSlots) % kSlots]);
      for (auto& n : cascade) {
        if (n->canceled) continue;
        place(n);
      }
    }
    fireSlot(l0_[nowTick_ % kSlots]);
  }
}

}  // namespace gs::timer
