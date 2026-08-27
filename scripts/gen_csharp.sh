#!/usr/bin/env bash
# 用与服务端一致的 protoc(3.21.12)生成 C# 协议代码给 Unity 客户端
set -eu
cd "$(dirname "$0")/.."
OUT="${1:-/mnt/f/unitypj/gs-client/gsclient/Assets/Scripts/Proto}"
mkdir -p "$OUT"
protoc --version
protoc -I src/proto --csharp_out="$OUT" game.proto
ls -la "$OUT"
