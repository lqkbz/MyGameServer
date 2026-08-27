#include "storage/MySqlStore.h"

#include <mysql/mysql.h>

#include <cstdio>
#include <vector>

namespace gs::storage {

MySqlStore::~MySqlStore() {
  if (conn_) mysql_close(conn_);
}

bool MySqlStore::connect(const std::string& host, const std::string& user,
                         const std::string& pass, const std::string& db) {
  if (conn_) mysql_close(conn_);
  conn_ = mysql_init(nullptr);
  unsigned int timeout = 2;  // 服务不在时快速失败
  mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
  if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), pass.c_str(),
                          db.c_str(), 3306, nullptr, 0)) {
    mysql_close(conn_);
    conn_ = nullptr;
    return false;
  }
  return true;
}

bool MySqlStore::ensureSchema() {
  return exec(
      "CREATE TABLE IF NOT EXISTS players ("
      "  account VARCHAR(64) PRIMARY KEY,"
      "  wins INT NOT NULL DEFAULT 0,"
      "  losses INT NOT NULL DEFAULT 0,"
      "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP "
      "    ON UPDATE CURRENT_TIMESTAMP)");
}

std::string MySqlStore::escape(const std::string& s) {
  std::vector<char> buf(s.size() * 2 + 1);  // 官方要求的最大膨胀
  unsigned long n =
      mysql_real_escape_string(conn_, buf.data(), s.data(), s.size());
  return std::string(buf.data(), n);
}

bool MySqlStore::exec(const std::string& sql) {
  if (!conn_) return false;
  if (mysql_query(conn_, sql.c_str()) != 0) {
    std::fprintf(stderr, "[mysql] %s\n", mysql_error(conn_));
    return false;
  }
  return true;
}

bool MySqlStore::recordResult(const std::string& winnerAcc,
                              const std::string& loserAcc) {
  // UPSERT 累加:不存在则插入,存在则 +1
  bool ok = exec("INSERT INTO players(account, wins) VALUES('" +
                 escape(winnerAcc) +
                 "', 1) ON DUPLICATE KEY UPDATE wins = wins + 1");
  ok = exec("INSERT INTO players(account, losses) VALUES('" +
            escape(loserAcc) +
            "', 1) ON DUPLICATE KEY UPDATE losses = losses + 1") &&
       ok;
  return ok;
}

std::optional<std::pair<int, int>> MySqlStore::getRecord(
    const std::string& account) {
  if (!exec("SELECT wins, losses FROM players WHERE account='" +
            escape(account) + "'")) {
    return std::nullopt;
  }
  MYSQL_RES* res = mysql_store_result(conn_);
  if (!res) return std::nullopt;
  std::optional<std::pair<int, int>> out;
  if (MYSQL_ROW row = mysql_fetch_row(res)) {
    out = std::make_pair(std::atoi(row[0]), std::atoi(row[1]));
  }
  mysql_free_result(res);
  return out;
}

bool MySqlStore::removeAccount(const std::string& account) {
  return exec("DELETE FROM players WHERE account='" + escape(account) + "'");
}

}  // namespace gs::storage
