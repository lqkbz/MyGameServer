#include "storage/RedisClient.h"

#include <hiredis/hiredis.h>

#include <cstdlib>

namespace gs::storage {

namespace {
// RAII 释放 redisReply
struct ReplyGuard {
  redisReply* r;
  ~ReplyGuard() {
    if (r) freeReplyObject(r);
  }
};
}  // namespace

RedisClient::~RedisClient() {
  if (ctx_) redisFree(ctx_);
}

bool RedisClient::connect(const std::string& host, int port) {
  if (ctx_) redisFree(ctx_);
  timeval tv{1, 0};  // 1s 连接超时:服务不在时快速失败
  ctx_ = redisConnectWithTimeout(host.c_str(), port, tv);
  return ctx_ && !ctx_->err;
}

bool RedisClient::ping() {
  if (!ctx_) return false;
  auto* r = static_cast<redisReply*>(redisCommand(ctx_, "PING"));
  ReplyGuard g{r};
  return r && r->type == REDIS_REPLY_STATUS;
}

bool RedisClient::zincrby(const std::string& key, const std::string& member,
                          double delta) {
  auto* r = static_cast<redisReply*>(
      redisCommand(ctx_, "ZINCRBY %s %f %s", key.c_str(), delta, member.c_str()));
  ReplyGuard g{r};
  return r && r->type != REDIS_REPLY_ERROR;
}

std::vector<std::pair<std::string, double>> RedisClient::ztopN(
    const std::string& key, int n) {
  std::vector<std::pair<std::string, double>> out;
  auto* r = static_cast<redisReply*>(redisCommand(
      ctx_, "ZREVRANGE %s 0 %d WITHSCORES", key.c_str(), n - 1));
  ReplyGuard g{r};
  if (!r || r->type != REDIS_REPLY_ARRAY) return out;
  for (size_t i = 0; i + 1 < r->elements; i += 2) {  // member,score 成对
    out.emplace_back(r->element[i]->str,
                     std::atof(r->element[i + 1]->str));
  }
  return out;
}

bool RedisClient::set(const std::string& key, const std::string& val) {
  auto* r = static_cast<redisReply*>(
      redisCommand(ctx_, "SET %s %s", key.c_str(), val.c_str()));
  ReplyGuard g{r};
  return r && r->type == REDIS_REPLY_STATUS;
}

bool RedisClient::del(const std::string& key) {
  auto* r =
      static_cast<redisReply*>(redisCommand(ctx_, "DEL %s", key.c_str()));
  ReplyGuard g{r};
  return r && r->type == REDIS_REPLY_INTEGER;
}

bool RedisClient::exists(const std::string& key) {
  auto* r =
      static_cast<redisReply*>(redisCommand(ctx_, "EXISTS %s", key.c_str()));
  ReplyGuard g{r};
  return r && r->type == REDIS_REPLY_INTEGER && r->integer == 1;
}

}  // namespace gs::storage
