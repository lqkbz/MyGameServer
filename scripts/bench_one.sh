#!/usr/bin/env bash
# 单场景压测: bench_one.sh mode conns duration port
set -u
cd "$(dirname "$0")/.."
ulimit -n 65535
mode=$1 conns=$2 dur=$3 port=$4
./build/game_server "$port" 4 4 >/tmp/srv_one.log 2>&1 &
SRV=$!
sleep 1
c0=$(awk '{print $14+$15}' "/proc/$SRV/stat"); t0=$(date +%s%N)
./build/bench_client --port "$port" --conns "$conns" --threads 4 \
    --mode "$mode" --duration "$dur" --ramp-us 100
c1=$(awk '{print $14+$15}' "/proc/$SRV/stat"); t1=$(date +%s%N)
hz=$(getconf CLK_TCK); e=$(( (t1 - t0) / 1000000 ))
awk -v d=$((c1-c0)) -v hz="$hz" -v e="$e" \
    'BEGIN{printf "server_cpu=%.0f%%\n", d*1000.0/hz/e*100}'
kill -TERM $SRV 2>/dev/null
wait $SRV 2>/dev/null
