#!/usr/bin/env bash
# 起服务器 + 两个 bot 打一局;用法: demo_battle.sh [port] [botB额外参数...]
set -u
cd "$(dirname "$0")/.."
PORT="${1:-9100}"
shift 2>/dev/null || true
./build/game_server "$PORT" 2 2 &
SRV=$!
sleep 0.3
./build/bot_client "$PORT" alice > /tmp/bot_a.log 2>&1 &
A=$!
./build/bot_client "$PORT" bob "$@" > /tmp/bot_b.log 2>&1 &
B=$!
wait $A; wait $B
kill $SRV 2>/dev/null
echo "===== alice ====="; tail -6 /tmp/bot_a.log
echo "===== bob ====="; tail -6 /tmp/bot_b.log
