#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "net/Callbacks.h"

namespace gs::game {

// 玩家会话(仅 lobby 线程读写)。
// 连接(TcpConnection)与会话(Session)分离是断线重连的基础:
// 连接断了 session 不销毁,保留 30s 等 token 重连重新绑定。
struct Session {
  enum State { kLobby, kMatching, kInRoom };

  uint64_t playerId = 0;
  std::string token;      // 重连凭证,登录时下发
  std::string account;
  net::TcpConnectionPtr conn;  // 断线期间为 nullptr
  uint64_t roomId = 0;         // 0 = 不在房间
  State state = kLobby;
  uint64_t lastHeartbeatMs = 0;   // 最近心跳(单调钟)
  uint64_t expireTimerId = 0;     // 断线后的 30s 保留定时器
};

using SessionPtr = std::shared_ptr<Session>;

}  // namespace gs::game
