#include "game/BattleSim.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace gs::game {

void BattleSim::addPlayer(uint64_t id, float x, float y) {
  P p;
  p.id = id;
  p.x = x;
  p.y = y;
  p.facingX = x <= kArena * 0.5f ? 1.f : -1.f;
  players_[id] = std::move(p);
}

void BattleSim::applyInput(uint64_t id, const pb::PlayerInput& in) {
  auto it = players_.find(id);
  if (it == players_.end()) return;
  it->second.input = in;
  it->second.hasInput = true;
  it->second.seq = in.seq();
}

std::vector<uint64_t> BattleSim::step(uint32_t dtMs) {
  std::set<uint64_t> changed;
  const float dt = dtMs / 1000.f;
  removedProjectileIds_.clear();

  // 仅推进 tick 开始前已经存在的子弹；本 tick 新生成的子弹会先被快照展示，
  // 从下一个 tick 开始飞行，保证近距离射击也至少可见一帧。
  std::vector<uint64_t> projectilesToAdvance;
  projectilesToAdvance.reserve(projectiles_.size());
  for (const auto& [id, projectile] : projectiles_) {
    (void)projectile;
    projectilesToAdvance.push_back(id);
  }

  // 1) 移动(死人不动)
  for (auto& [id, p] : players_) {
    if (!p.alive() || !p.hasInput) continue;
    float mx = p.input.move_x(), my = p.input.move_y();
    float len = std::sqrt(mx * mx + my * my);
    if (len < 1e-6f) continue;
    if (len > 1.f) {  // 客户端不可信:模长超 1 的输入压回单位向量
      mx /= len;
      my /= len;
    }
    p.facingX = mx;
    p.facingY = my;
    float nx = std::clamp(p.x + mx * kSpeed * dt, 0.f, kArena);
    float ny = std::clamp(p.y + my * kSpeed * dt, 0.f, kArena);
    if (nx != p.x || ny != p.y) {
      p.x = nx;
      p.y = ny;
      changed.insert(id);
    }
  }

  // 2) CD 衰减
  for (auto& [id, p] : players_) p.cdMs = std::max(0.f, p.cdMs - dtMs);

  // 3) 结算攻击:每次 CD 到期生成一颗权威子弹。
  // 优先朝最近的存活敌人瞄准；没有目标时沿最近移动方向发射。
  for (auto& [id, p] : players_) {
    if (!p.alive() || !p.hasInput || !p.input.attack() || p.cdMs > 0) continue;
    P* target = nullptr;
    float best = INFINITY;
    for (auto& [tid, t] : players_) {
      if (tid == id || !t.alive()) continue;
      float d = std::hypot(t.x - p.x, t.y - p.y);
      if (d < best) {
        best = d;
        target = &t;
      }
    }
    float dx = p.facingX, dy = p.facingY;
    if (target) {
      dx = target->x - p.x;
      dy = target->y - p.y;
      float len = std::hypot(dx, dy);
      if (len > 1e-6f) {
        dx /= len;
        dy /= len;
      }
    }
    Projectile projectile;
    projectile.id = nextProjectileId_++;
    projectile.ownerId = id;
    projectile.x = p.x + dx * 2.f;
    projectile.y = p.y + dy * 2.f;
    projectile.vx = dx * kProjectileSpeed;
    projectile.vy = dy * kProjectileSpeed;
    projectiles_[projectile.id] = projectile;
    p.cdMs = kAttackCd;
  }

  // 4) 推进子弹并做线段-圆碰撞，避免高速子弹跨 tick 穿透玩家。
  for (uint64_t projectileId : projectilesToAdvance) {
    auto projectileIt = projectiles_.find(projectileId);
    if (projectileIt == projectiles_.end()) continue;
    Projectile& projectile = projectileIt->second;
    const float oldX = projectile.x;
    const float oldY = projectile.y;
    const float newX = oldX + projectile.vx * dt;
    const float newY = oldY + projectile.vy * dt;
    const float segX = newX - oldX;
    const float segY = newY - oldY;
    const float segLenSq = segX * segX + segY * segY;

    P* hit = nullptr;
    float hitT = INFINITY;
    for (auto& [targetId, target] : players_) {
      if (targetId == projectile.ownerId || !target.alive()) continue;
      float t = 0.f;
      if (segLenSq > 1e-6f) {
        t = ((target.x - oldX) * segX + (target.y - oldY) * segY) / segLenSq;
        t = std::clamp(t, 0.f, 1.f);
      }
      const float closestX = oldX + segX * t;
      const float closestY = oldY + segY * t;
      const float distanceX = target.x - closestX;
      const float distanceY = target.y - closestY;
      if (distanceX * distanceX + distanceY * distanceY <=
              kProjectileRadius * kProjectileRadius &&
          t < hitT) {
        hit = &target;
        hitT = t;
      }
    }

    bool remove = false;
    if (hit) {
      hit->hp = std::max(0, hit->hp - kDamage);
      changed.insert(hit->id);
      remove = true;
    } else {
      projectile.x = newX;
      projectile.y = newY;
      projectile.traveled += std::sqrt(segLenSq);
      remove = projectile.traveled >= kProjectileRange || newX < 0.f ||
               newX > kArena || newY < 0.f || newY > kArena;
    }

    if (remove) {
      removedProjectileIds_.push_back(projectileId);
      projectiles_.erase(projectileIt);
    }
  }

  return {changed.begin(), changed.end()};
}

uint64_t BattleSim::winnerOrZero() const {
  uint64_t last = 0;
  int alive = 0;
  for (auto& [id, p] : players_) {
    if (p.alive()) {
      ++alive;
      last = id;
    }
  }
  return alive == 1 ? last : 0;
}

void BattleSim::fillState(uint64_t id, pb::PlayerState* out) const {
  auto it = players_.find(id);
  if (it == players_.end()) return;
  const P& p = it->second;
  out->set_player_id(p.id);
  out->set_x(p.x);
  out->set_y(p.y);
  out->set_hp(p.hp);
  out->set_last_input_seq(p.seq);
}

void BattleSim::fillProjectileState(uint64_t id, pb::ProjectileState* out) const {
  auto it = projectiles_.find(id);
  if (it == projectiles_.end()) return;
  const Projectile& projectile = it->second;
  out->set_projectile_id(projectile.id);
  out->set_owner_id(projectile.ownerId);
  out->set_x(projectile.x);
  out->set_y(projectile.y);
  out->set_velocity_x(projectile.vx);
  out->set_velocity_y(projectile.vy);
}

std::vector<uint64_t> BattleSim::playerIds() const {
  std::vector<uint64_t> ids;
  for (auto& [id, p] : players_) ids.push_back(id);
  return ids;
}

std::vector<uint64_t> BattleSim::projectileIds() const {
  std::vector<uint64_t> ids;
  ids.reserve(projectiles_.size());
  for (const auto& [id, projectile] : projectiles_) {
    (void)projectile;
    ids.push_back(id);
  }
  return ids;
}

std::vector<uint64_t> BattleSim::takeRemovedProjectileIds() {
  return std::exchange(removedProjectileIds_, {});
}

}  // namespace gs::game
