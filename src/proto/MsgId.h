#pragma once
#include <cstdint>

namespace gs::proto {

// 帧头 msgId ↔ pb 消息类型 对照表（收发两侧共用）
enum MsgId : uint16_t {
  kEcho = 1,
  kHeartbeat = 2,
  kLoginReq = 3,
  kLoginResp = 4,
  kReconnectReq = 5,
  kMatchReq = 10,
  kEnterRoom = 11,
  kPlayerInput = 20,
  kStateSnapshot = 21,
  kBattleEnd = 22,
  kLeaderboardReq = 30,
  kLeaderboardResp = 31,
};

}  // namespace gs::proto
