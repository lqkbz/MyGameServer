#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_set>
#include <utility>

namespace gs::game {

// 匹配队列(纯逻辑,仅 lobby 线程访问,无锁)。
// FIFO 凑对;deque 存顺序,set 去重;remove 用惰性删除:
// 只从 set 摘除,deque 里的残留在 popPair 时跳过。
class MatchQueue {
 public:
  // 入队;已在队中返回 false
  bool push(uint64_t playerId) {
    if (!waiting_.insert(playerId).second) return false;
    order_.push_back(playerId);
    return true;
  }

  // 断线/超时出队
  void remove(uint64_t playerId) { waiting_.erase(playerId); }

  bool contains(uint64_t playerId) const { return waiting_.count(playerId); }

  // 凑齐两人则弹出(FIFO),否则 nullopt
  std::optional<std::pair<uint64_t, uint64_t>> popPair() {
    compact();
    if (waiting_.size() < 2) return std::nullopt;
    uint64_t a = takeFront();
    uint64_t b = takeFront();
    return std::make_pair(a, b);
  }

 private:
  void compact() {  // 清掉队头已 remove 的惰性残留
    while (!order_.empty() && !waiting_.count(order_.front())) {
      order_.pop_front();
    }
  }
  uint64_t takeFront() {
    compact();
    uint64_t id = order_.front();
    order_.pop_front();
    waiting_.erase(id);
    return id;
  }

  std::deque<uint64_t> order_;
  std::unordered_set<uint64_t> waiting_;
};

}  // namespace gs::game
