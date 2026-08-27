#!/usr/bin/env bash
# battle 负载下采样服务端热点(perf 版本与 WSL 内核不匹配,基本采样仍可用)
set -u
cd "$(dirname "$0")/.."
PERF=/usr/lib/linux-tools-6.8.0-138/perf
ulimit -n 65535
cp build/game_server /tmp/game_server_perf  # 原生 ext4 上 perf 才能解符号
/tmp/game_server_perf 9220 4 4 >/tmp/srv_perf.log 2>&1 &
SRV=$!
sleep 1
./build/bench_client --port 9220 --conns 2000 --threads 4 --mode battle \
    --duration 16 >/tmp/bc_perf.log 2>&1 &
BC=$!
sleep 4
$PERF record -F 199 -p "$SRV" -g -o /tmp/perf.data -- sleep 8 2>&1 | tail -1
wait $BC
kill -TERM $SRV 2>/dev/null
wait $SRV 2>/dev/null
$PERF report -i /tmp/perf.data --stdio --percent-limit 2 2>/dev/null |
    grep -vE '^#|^$' | head -25
