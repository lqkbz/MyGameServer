// 对战服务器入口。用法: ./game_server [port] [io线程] [game线程]
#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "game/GameServer.h"
#include "net/EventLoop.h"

namespace {
gs::net::EventLoop* g_loop = nullptr;
// SIGTERM/SIGINT → 退出事件循环 → GameServer 析构 flush 存储队列。
// 直接被杀会丢队列里未落库的写,优雅退出是异步写库的配套动作。
void handleSignal(int) {
  if (g_loop) g_loop->quit();
}
}  // namespace

int main(int argc, char* argv[]) {
  uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 9100;
  int io = argc > 2 ? std::atoi(argv[2]) : 2;
  int game = argc > 3 ? std::atoi(argv[3]) : 2;

  gs::net::EventLoop loop;  // 主 Reactor:只 accept
  gs::game::StorageOptions store;
  store.enabled = true;  // 连不上会自动降级为纯内存
  gs::game::GameServer server(&loop, port, io, game, store);
  server.start();
  g_loop = &loop;
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  std::printf("game_server on %u (io=%d, game=%d)\n", port, io, game);
  loop.loop();
  std::printf("shutting down, flushing storage queue...\n");
}
