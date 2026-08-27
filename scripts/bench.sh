#!/usr/bin/env bash
# 压测矩阵。用法: bench.sh [结果文件]
set -u
cd "$(dirname "$0")/.."
OUT="${1:-/tmp/bench_results.txt}"
: > "$OUT"
ulimit -n 65535

cpu_of() {  # $1=pid → 累计 cpu ticks(utime+stime)
  awk '{print $14+$15}' "/proc/$1/stat"
}

run_case() {  # $1=mode $2=conns $3=threads $4=duration $5=port
  local mode=$1 conns=$2 threads=$3 dur=$4 port=$5
  ./build/game_server "$port" 4 4 >/tmp/srv_bench.log 2>&1 &
  local SRV=$!
  sleep 1
  local c0 t0 c1 t1
  c0=$(cpu_of $SRV); t0=$(date +%s%N)
  ./build/bench_client --port "$port" --conns "$conns" --threads "$threads" \
      --mode "$mode" --duration "$dur" --ramp-us 100 >> "$OUT" 2>&1
  c1=$(cpu_of $SRV); t1=$(date +%s%N)
  local hz elapsed cpu
  hz=$(getconf CLK_TCK)
  elapsed=$(( (t1 - t0) / 1000000 ))  # ms
  cpu=$(awk -v d=$((c1-c0)) -v hz="$hz" -v e="$elapsed" \
        'BEGIN{printf "%.0f", d*1000.0/hz/e*100}')
  echo "server_cpu=${cpu}% (elapsed ${elapsed}ms, cores=$(nproc))" >> "$OUT"
  echo "----" >> "$OUT"
  kill -TERM $SRV 2>/dev/null
  wait $SRV 2>/dev/null
  sleep 0.5
}

echo "== idle scaling ==" >> "$OUT"
run_case idle 1000 4 15 9210
run_case idle 4000 4 15 9211
run_case idle 8000 4 15 9212

echo "== battle scaling ==" >> "$OUT"
run_case battle 200 4 20 9213
run_case battle 1000 4 20 9214
run_case battle 2000 4 20 9215

cat "$OUT"
