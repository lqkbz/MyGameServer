#pragma once
#include <optional>
#include <string>
#include <utility>

typedef struct MYSQL MYSQL;  // libmysqlclient 前向声明

namespace gs::storage {

// MySQL 玩家存档。仅存储线程使用 → 无锁。
// 表: players(account PK, wins, losses) — UPSERT 累加,天然幂等友好。
class MySqlStore {
 public:
  MySqlStore() = default;
  ~MySqlStore();
  MySqlStore(const MySqlStore&) = delete;
  MySqlStore& operator=(const MySqlStore&) = delete;

  bool connect(const std::string& host, const std::string& user,
               const std::string& pass, const std::string& db);
  bool ensureSchema();
  // 胜负各 +1(两条 UPSERT,同一连接顺序执行)
  bool recordResult(const std::string& winnerAcc, const std::string& loserAcc);
  // {wins, losses};无记录返回 nullopt
  std::optional<std::pair<int, int>> getRecord(const std::string& account);
  bool removeAccount(const std::string& account);  // 测试清理用

 private:
  std::string escape(const std::string& s);  // mysql_real_escape_string 防注入
  bool exec(const std::string& sql);

  MYSQL* conn_ = nullptr;
};

}  // namespace gs::storage
