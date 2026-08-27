#!/usr/bin/env bash
# 构建 + 跑测试;用法: run_tests.sh [gtest_filter]
set -u
cd "$(dirname "$0")/.."
cmake -B build >/dev/null 2>&1
if ! cmake --build build -j 2>/tmp/build.log; then
  grep -E "error" -A2 /tmp/build.log | head -30
  echo "BUILD_FAILED"
  exit 1
fi
FILTER="${1:-*}"
timeout 120 ./build/gs_tests --gtest_filter="$FILTER" >/tmp/gt.log 2>&1
RC=$?
echo "exit_code=$RC"
grep -E "\[ +RUN|\[ +OK|\[ +FAILED|PASSED|core" /tmp/gt.log | tail -12
