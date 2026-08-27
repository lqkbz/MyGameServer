#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct redisContext;  // hiredis 前向声明,避免头文件扩散

namespace gs::storage {

// hiredis 同步客户端薄封装。仅存储线程(AsyncWriter)使用 → 无锁。
// 同步 API 够用:写路径本来就在专用线程排队,阻塞不影响业务线程。
class RedisClient {
 public:
  RedisClient() = default;
  ~RedisClient();
  RedisClient(const RedisClient&) = delete;
  RedisClient& operator=(const RedisClient&) = delete;

  bool connect(const std::string& host, int port);
  bool ping();

  bool zincrby(const std::string& key, const std::string& member, double delta);
  // 按分数降序取前 n:{member, score}
  std::vector<std::pair<std::string, double>> ztopN(const std::string& key,
                                                    int n);
  bool set(const std::string& key, const std::string& val);
  bool del(const std::string& key);
  bool exists(const std::string& key);

 private:
  redisContext* ctx_ = nullptr;
};

}  // namespace gs::storage
