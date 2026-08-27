// 压测客户端:多线程 epoll(ET) 模拟海量长连接。
// 用法: ./bench_client --port P --conns N --threads M --mode idle|battle
//                      --duration 秒 [--ramp-us 每连接间隔微秒]
// idle:   登录 + 2s 心跳,测长连接容量与心跳 RTT
// battle: 全员匹配进房,只移动不攻击(战斗不结束),每 100ms 发输入,
//         测"发输入 → 收到回带 last_input_seq 的快照"延迟(输入上屏延迟)
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game.pb.h"
#include "net/Buffer.h"
#include "proto/MsgId.h"
#include "proto/ProtobufCodec.h"

using namespace gs;
using proto::MsgId;
using proto::ProtobufCodec;

namespace {

uint64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct Options {
  uint16_t port = 9100;
  int conns = 100;
  int threads = 2;
  bool battle = false;
  int durationSec = 10;
  int rampUs = 200;  // 每连接建立间隔,防 SYN 洪泛丢连接
};

struct Conn {
  int fd = -1;
  enum State { kConnecting, kWaitLogin, kIdle, kMatching, kBattle, kDead };
  State state = kConnecting;
  net::Buffer in;
  std::string out;  // 未写完的字节,等 EPOLLOUT 续写
  uint64_t playerId = 0;
  uint64_t nextHbMs = 0;
  uint64_t nextInputMs = 0;
  uint32_t seq = 0;
  uint32_t pendingSeq = 0;      // 正在测量的输入序号
  uint64_t pendingSentMs = 0;   // 其发出时刻
};

struct Stats {
  size_t established = 0, failed = 0, dead = 0;
  size_t sent = 0, recvd = 0;
  std::vector<double> hbRtt;     // 心跳往返 ms
  std::vector<double> inputRtt;  // 输入→快照 ms
};

// —— 前向声明:worker 主体在下方实现 ——
void worker(int tid, const Options& opt, Stats* out, std::mutex* outMu,
            std::atomic<bool>* stopFlag);

double pct(std::vector<double>& v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  size_t i = static_cast<size_t>(p / 100.0 * (v.size() - 1));
  return v[i];
}

}  // namespace

int main(int argc, char* argv[]) {
  Options opt;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string k = argv[i];
    const char* v = argv[i + 1];
    if (k == "--port") opt.port = static_cast<uint16_t>(std::atoi(v));
    else if (k == "--conns") opt.conns = std::atoi(v);
    else if (k == "--threads") opt.threads = std::atoi(v);
    else if (k == "--mode") opt.battle = (std::string(v) == "battle");
    else if (k == "--duration") opt.durationSec = std::atoi(v);
    else if (k == "--ramp-us") opt.rampUs = std::atoi(v);
  }

  Stats total;
  std::mutex mu;
  std::atomic<bool> stop{false};
  std::vector<std::thread> ts;
  uint64_t t0 = nowMs();
  for (int t = 0; t < opt.threads; ++t) {
    ts.emplace_back(worker, t, std::cref(opt), &total, &mu, &stop);
  }
  std::this_thread::sleep_for(std::chrono::seconds(opt.durationSec));
  stop = true;
  for (auto& t : ts) t.join();
  uint64_t elapsed = nowMs() - t0;

  std::printf("---- bench summary ----\n");
  std::printf("mode=%s conns=%d threads=%d duration=%.1fs\n",
              opt.battle ? "battle" : "idle", opt.conns, opt.threads,
              elapsed / 1000.0);
  std::printf("established=%zu failed=%zu dead=%zu\n", total.established,
              total.failed, total.dead);
  std::printf("msgs sent=%zu recvd=%zu (recv %.0f/s)\n", total.sent,
              total.recvd, total.recvd * 1000.0 / elapsed);
  std::printf("heartbeat RTT ms: n=%zu p50=%.2f p90=%.2f p99=%.2f max=%.2f\n",
              total.hbRtt.size(), pct(total.hbRtt, 50), pct(total.hbRtt, 90),
              pct(total.hbRtt, 99), pct(total.hbRtt, 100));
  if (opt.battle) {
    std::printf(
        "input->snapshot ms: n=%zu p50=%.2f p90=%.2f p99=%.2f max=%.2f\n",
        total.inputRtt.size(), pct(total.inputRtt, 50),
        pct(total.inputRtt, 90), pct(total.inputRtt, 99),
        pct(total.inputRtt, 100));
  }
  return 0;
}

// ==== worker 实现 ====
namespace {

void setEvents(int ep, Conn* c, bool wantOut) {
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET | (wantOut ? static_cast<uint32_t>(EPOLLOUT) : 0u);
  ev.data.ptr = c;
  ::epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);
}

// 追加待发数据并尽量写出;写不完挂 EPOLLOUT
void sendMsg(int ep, Conn* c, uint16_t id, const google::protobuf::Message& m,
             Stats* st) {
  c->out += ProtobufCodec::encode(id, m);
  while (!c->out.empty()) {
    ssize_t n = ::send(c->fd, c->out.data(), c->out.size(), 0);
    if (n > 0) {
      c->out.erase(0, n);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else {
      c->state = Conn::kDead;
      return;
    }
  }
  ++st->sent;
  setEvents(ep, c, !c->out.empty());
}

void handleFrame(int ep, Conn* c, uint16_t id, const char* d, size_t n,
                 const Options& opt, Stats* st) {
  switch (id) {
    case MsgId::kLoginResp: {
      pb::LoginResp r;
      if (!r.ParseFromArray(d, static_cast<int>(n)) || r.code() != 0) break;
      c->playerId = r.player_id();
      if (opt.battle) {
        pb::MatchReq mr;
        mr.set_player_id(c->playerId);
        sendMsg(ep, c, MsgId::kMatchReq, mr, st);
        c->state = Conn::kMatching;
      } else {
        c->state = Conn::kIdle;
      }
      break;
    }
    case MsgId::kHeartbeat: {
      pb::Heartbeat hb;
      if (hb.ParseFromArray(d, static_cast<int>(n))) {
        st->hbRtt.push_back(static_cast<double>(
            nowMs() - static_cast<uint64_t>(hb.client_ms())));
      }
      break;
    }
    case MsgId::kEnterRoom:
      c->state = Conn::kBattle;
      break;
    case MsgId::kStateSnapshot: {
      if (c->pendingSeq == 0) break;
      pb::StateSnapshot snap;
      if (!snap.ParseFromArray(d, static_cast<int>(n))) break;
      for (const auto& p : snap.players()) {
        if (p.player_id() == c->playerId &&
            p.last_input_seq() >= c->pendingSeq) {
          st->inputRtt.push_back(
              static_cast<double>(nowMs() - c->pendingSentMs));
          c->pendingSeq = 0;
          break;
        }
      }
      break;
    }
    default:
      break;
  }
}

// ET 读到 EAGAIN,循环切帧
void onReadable(int ep, Conn* c, const Options& opt, Stats* st) {
  while (true) {
    int savedErrno = 0;
    ssize_t n = c->in.readFd(c->fd, &savedErrno);
    if (n == 0) {
      c->state = Conn::kDead;
      return;
    }
    if (n < 0) {
      if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) break;
      if (savedErrno == EINTR) continue;
      c->state = Conn::kDead;
      return;
    }
  }
  while (c->in.readableBytes() >= 6) {
    uint32_t netLen;
    std::memcpy(&netLen, c->in.peek(), 4);
    uint32_t len = ntohl(netLen);
    if (c->in.readableBytes() < 4 + len) break;
    uint16_t netId;
    std::memcpy(&netId, c->in.peek() + 4, 2);
    ++st->recvd;
    handleFrame(ep, c, ntohs(netId), c->in.peek() + 6, len - 2, opt, st);
    c->in.retrieve(4 + len);
  }
}

void worker(int tid, const Options& opt, Stats* out, std::mutex* outMu,
            std::atomic<bool>* stopFlag) {
  Stats st;
  int ep = ::epoll_create1(EPOLL_CLOEXEC);
  int myConns = opt.conns / opt.threads +
                (tid < opt.conns % opt.threads ? 1 : 0);
  std::vector<Conn> conns(myConns);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(opt.port);
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  // 渐进建连:非阻塞 connect,EPOLLOUT 到来即成功
  for (int i = 0; i < myConns; ++i) {
    Conn& c = conns[i];
    c.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (c.fd < 0) {
      c.state = Conn::kDead;
      ++st.failed;
      continue;
    }
    int r = ::connect(c.fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (r < 0 && errno != EINPROGRESS) {
      c.state = Conn::kDead;
      ++st.failed;
      ::close(c.fd);
      c.fd = -1;
      continue;
    }
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = &c;
    ::epoll_ctl(ep, EPOLL_CTL_ADD, c.fd, &ev);
    if (opt.rampUs > 0) ::usleep(opt.rampUs);
  }

  epoll_event evs[512];
  uint64_t hbStagger = 0;  // 心跳错峰,避免同一毫秒齐发
  while (!stopFlag->load(std::memory_order_relaxed)) {
    int n = ::epoll_wait(ep, evs, 512, 5);
    for (int i = 0; i < n; ++i) {
      auto* c = static_cast<Conn*>(evs[i].data.ptr);
      if (c->state == Conn::kDead) continue;
      if (c->state == Conn::kConnecting && (evs[i].events & EPOLLOUT)) {
        int err = 0;
        socklen_t elen = sizeof err;
        ::getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) {
          c->state = Conn::kDead;
          ++st.failed;
          continue;
        }
        ++st.established;
        c->state = Conn::kWaitLogin;
        pb::LoginReq req;
        req.set_account("bench_" + std::to_string(tid) + "_" +
                        std::to_string(c - conns.data()));
        sendMsg(ep, c, MsgId::kLoginReq, req, &st);
        c->nextHbMs = nowMs() + 500 + (hbStagger++ % 2000);
      }
      if (evs[i].events & EPOLLIN) onReadable(ep, c, opt, &st);
      if (c->state != Conn::kDead && c->state != Conn::kConnecting &&
          (evs[i].events & EPOLLOUT) && !c->out.empty()) {
        while (!c->out.empty()) {  // 续写上次没写完的

          ssize_t w = ::send(c->fd, c->out.data(), c->out.size(), 0);
          if (w > 0) c->out.erase(0, w);
          else break;
        }
        setEvents(ep, c, !c->out.empty());
      }
    }

    // 时间驱动:心跳 / 战斗输入
    uint64_t now = nowMs();
    for (auto& c : conns) {
      if (c.state == Conn::kDead || c.state == Conn::kConnecting ||
          c.state == Conn::kWaitLogin) {
        continue;
      }
      if (now >= c.nextHbMs) {
        c.nextHbMs = now + 2000;
        pb::Heartbeat hb;
        hb.set_client_ms(static_cast<int64_t>(now));
        sendMsg(ep, &c, MsgId::kHeartbeat, hb, &st);
      }
      if (c.state == Conn::kBattle && now >= c.nextInputMs) {
        c.nextInputMs = now + 100;
        // 八方向巡游,每秒换向:持续移动 → 快照持续有增量
        static const float dir[8][2] = {{1, 0},   {0.7f, 0.7f}, {0, 1},
                                        {-0.7f, 0.7f}, {-1, 0}, {-0.7f, -0.7f},
                                        {0, -1},  {0.7f, -0.7f}};
        int di = static_cast<int>((now / 1000 + c.fd) % 8);
        pb::PlayerInput in;
        in.set_seq(++c.seq);
        in.set_move_x(dir[di][0]);
        in.set_move_y(dir[di][1]);
        in.set_attack(false);  // 不攻击:战斗不结束,压稳态
        sendMsg(ep, &c, MsgId::kPlayerInput, in, &st);
        if (c.pendingSeq == 0) {  // 一次只测一个在途输入
          c.pendingSeq = c.seq;
          c.pendingSentMs = now;
        }
      }
    }
  }

  for (auto& c : conns) {
    if (c.fd >= 0) ::close(c.fd);
    if (c.state == Conn::kDead) ++st.dead;
  }
  ::close(ep);

  std::lock_guard<std::mutex> lk(*outMu);
  out->established += st.established;
  out->failed += st.failed;
  out->dead += st.dead;
  out->sent += st.sent;
  out->recvd += st.recvd;
  out->hbRtt.insert(out->hbRtt.end(), st.hbRtt.begin(), st.hbRtt.end());
  out->inputRtt.insert(out->inputRtt.end(), st.inputRtt.begin(),
                       st.inputRtt.end());
}

}  // namespace
