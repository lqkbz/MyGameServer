# MyGameServer

一个面向学习、工程实践与面试展示的实时对战游戏项目：服务端使用
Linux C++17 自研 Reactor 网络库，客户端保留 Unity/C# 手写代码；双方通过
TCP + Protobuf 通信，由服务端以 20Hz 执行权威战斗模拟。

项目覆盖从网络底层到游戏业务的完整链路：连接管理、协议分帧、登录会话、
匹配、房间、状态同步、断线重连、异步存储、排行榜、自动化测试、Bot 和压测。

## 核心能力

| 方向 | 已实现内容 |
|---|---|
| 网络层 | `epoll` ET、主从 Reactor、one-loop-per-thread、非阻塞 IO、输入/输出缓冲、`eventfd` 跨线程唤醒 |
| 协议层 | 4 字节长度 + 2 字节消息号 + Protobuf；处理粘包、半包、非法帧与 64 KiB 上限 |
| 游戏业务 | 登录、心跳、匹配超时、双人房间、20Hz 权威模拟、移动、子弹、碰撞、伤害与结算 |
| 状态同步 | 增量快照；进房和重连发送全量快照；`last_input_seq` 用于输入确认 |
| 会话恢复 | 连接与 Session 解耦，断线后保留会话，使用 token 重连并恢复房间状态 |
| 存储 | 独立异步写线程；MySQL 保存战绩，Redis 保存在线状态和 ZSET 排行榜；不可用时对战降级运行 |
| 客户端 | Unity 后台收包线程、主线程 Pump、状态机、快照插值、角色/子弹渲染和 UI |
| 验证工具 | 55 个 C++ 测试、27 个 Unity 逻辑测试源码、Bot、端到端测试和多线程 epoll 压测客户端 |

## 实测结果

测试环境为 WSL2 Ubuntu 24.04、32 逻辑核，服务端与压测端同机回环，参数为
4 个 IO 线程 + 4 个 game 线程。数据用于说明本机实验结果，不代表公网或生产容量。

| 场景 | 结果 |
|---|---|
| 长连接 | 16,000 个连接全部成功；心跳 RTT P99 = 1 ms；服务端约 51% CPU（约 0.51 核） |
| 对战负载 | 2,000 玩家 / 1,000 房间；约 40,700 条收包/s；服务端约 115% CPU（约 1.15 核） |
| 输入确认 | 从发送输入到权威快照确认 `last_input_seq`，P99 = 51 ms，接近一个 50 ms tick |

> 心跳数据来自同机回环，不包含真实网络 RTT、丢包和跨机调度成本。

## 架构

```text
Unity 主线程
  GameUI / GameClient
      │ 用户输入、事件 Pump、渲染
      ▼
  GameSession ── SnapshotInterpolator ── PlayerView / ProjectileView
      ▲
      │ NetworkClient 后台收线程
      │ TCP: len(4B BE) + msgId(2B BE) + protobuf body
      ▼
C++ 服务端
  base EventLoop       主 Reactor，只负责 accept
      │ round-robin
      ▼
  IO EventLoop 池      socket 读写、分帧、Protobuf 解析
      │ runInLoop
      ▼
  lobby EventLoop      Session、匹配、房间路由、TimerWheel
      │ 房间固定绑定
      ▼
  game EventLoop 池    Room + BattleSim，20Hz 权威模拟
      │
      ├── TcpConnection::send → 对应 IO loop
      └── AsyncWriter → MySQL / Redis
```

### 线程归属

| 状态/资源 | 所属线程 |
|---|---|
| 监听 socket | base loop |
| 连接、收发缓冲、协议解析 | 连接所属 IO loop |
| Session、MatchQueue、房间表、TimerWheel | lobby loop |
| 单个 Room、BattleSim、20Hz 定时器 | 固定 game loop |
| MySQL/Redis 写操作 | AsyncWriter 存储线程 |

跨线程操作通过 `EventLoop::runInLoop` / `queueInLoop` 投递闭包，业务状态尽量依靠
线程隔离而非共享锁保护。

## 协议与状态同步

每个 TCP 帧采用大端编码：

```text
+-------------------+----------------+----------------------+
| len: uint32 (4B)  | msgId: uint16  | protobuf body        |
+-------------------+----------------+----------------------+
```

`len` 包含 `msgId + body`。消息定义位于
[`src/proto/game.proto`](src/proto/game.proto)，消息号位于
[`src/proto/MsgId.h`](src/proto/MsgId.h)。主要流程为：

```text
LoginReq → LoginResp → MatchReq → EnterRoom
         → PlayerInput ⇄ StateSnapshot → BattleEnd
ReconnectReq → LoginResp + EnterRoom + full StateSnapshot
```

客户端只发送移动和攻击意图；位置、弹道、碰撞、伤害和胜负均由服务端计算。
普通 tick 广播增量快照，首次进房或重连广播全量快照。

## 目录结构

```text
.
├─ src/net/       Reactor、EventLoop、Channel、TcpServer/TcpConnection
├─ src/proto/     Protobuf 帧编解码、Dispatcher、消息号和 game.proto
├─ src/timer/     timerfd 周期定时器与分层时间轮
├─ src/game/      Session、匹配、房间、战斗模拟和服务端组装
├─ src/storage/   AsyncWriter、RedisClient、MySqlStore
├─ examples/      服务端入口、echo、Bot 和压测客户端
├─ tests/         GTest 单元测试、集成测试和端到端测试
├─ scripts/       构建测试、演示、压测、存储初始化和 C# 协议生成
└─ client/        Unity 手写 Assets 与 EditMode 测试源码
```

## 环境依赖

推荐使用 WSL2 Ubuntu 24.04 或原生 Linux：

```bash
sudo apt update
sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev \
  libgtest-dev libhiredis-dev default-libmysqlclient-dev redis-server mysql-server
```

核心依赖：CMake 3.16+、支持 C++17 的编译器、Protobuf、GoogleTest、hiredis
和 MySQL Client。

## 构建与运行

### 1. 编译

```bash
cmake -S . -B build
cmake --build build -j
```

生成的主要程序：

| 程序 | 用途 |
|---|---|
| `build/game_server` | 实时对战服务端 |
| `build/echo_server` | 网络与协议回显示例 |
| `build/bot_client` | 自动登录、匹配和战斗的 Bot |
| `build/bench_client` | 多线程 epoll 压测客户端 |
| `build/gs_tests` | C++ 测试集合 |

### 2. 启动服务端

```bash
./build/game_server 9100 4 4
```

参数依次为：端口、IO 线程数、game 线程数。默认值为 `9100 2 2`。

服务端默认尝试连接本机 Redis/MySQL；连接失败时会降级为纯内存对战，登录、匹配
和战斗仍可运行，但在线状态、排行榜和持久化不可用。

### 3. 可选：启动本地存储

```bash
sudo bash scripts/start_storage.sh
```

该脚本面向本地开发环境，会创建示例数据库、账号和密码 `gs/gs123`，请勿直接用于
生产环境。

### 4. Bot 对战

分别在两个终端运行：

```bash
./build/bot_client 9100 alice
./build/bot_client 9100 bob
```

断线重连演示：

```bash
./build/bot_client 9100 alice --drop-after-ms 1500
```

也可运行自动化演示脚本：

```bash
bash scripts/demo_battle.sh 9100
```

## 测试

构建并运行完整 C++ 测试：

```bash
bash scripts/run_tests.sh
```

或使用标准 CMake/CTest 流程：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

测试覆盖 Buffer、EventLoop、线程池、TCP 生命周期、协议分帧、Dispatcher、
TimerWheel、PeriodicTimer、匹配、战斗、重连、Redis、MySQL、AsyncWriter 和完整
游戏链路。存储服务未运行时，相关外部依赖测试会跳过。

## 压测

```bash
bash scripts/bench.sh
bash scripts/bench_one.sh idle 16000 15 9231
bash scripts/bench_one.sh battle 2000 20 9232
sudo bash scripts/perf_battle.sh
```

`idle` 模式测量登录后心跳回显 RTT；`battle` 模式测量输入发出到收到
`last_input_seq >= seq` 权威快照的确认延迟。压测前请提高文件描述符限制，并避免
把同机回环结果等同于公网表现。

## Unity 客户端

仓库中的 [`client/`](client/) 只归档 Unity 工程的手写 `Assets` 与 EditMode
测试，不包含 `Library`、`Temp`、完整包缓存和生成的 `Game.cs`。

恢复步骤：

1. 使用 Unity 6000.x 创建 Universal 2D 工程。
2. 将 `client/Assets/` 复制到新工程的 `Assets/`。
3. 下载 Google.Protobuf 3.21.12，将 `lib/netstandard2.0/Google.Protobuf.dll`
   放入 `Assets/Plugins/`。
4. 使用同版本 `protoc` 生成 C# 协议代码：

   ```bash
   bash scripts/gen_csharp.sh /path/to/unity-project/Assets/Scripts/Proto
   ```

5. 启动服务端，在客户端中连接 `127.0.0.1:9100`。

操作：`W/A/S/D` 移动，`Space` 发射。客户端使用后台线程收包，在 Unity 主线程
执行消息 Pump、状态机更新、快照插值和渲染。

## 项目边界

这是单机学习型服务器，而不是可直接上线的生产集群。当前未实现或仍需加强：

- TLS、正式账号鉴权、网关、限流与反作弊；
- 存储 WAL、失败重试、有界背压、事务与 Redis/MySQL 补偿；
- 跨进程房间调度、服务发现、容灾、滚动升级和水平扩容；
- 完整监控、链路追踪、故障注入、跨机公网压测与长期稳定性测试；
- UDP/KCP、预测回滚、观战、回放和大房间广播优化。

## 推荐阅读顺序

1. [`src/proto/game.proto`](src/proto/game.proto) 与
   [`src/proto/MsgId.h`](src/proto/MsgId.h)：理解线上消息语义。
2. [`examples/game_server.cc`](examples/game_server.cc) 与
   [`src/game/GameServer.cc`](src/game/GameServer.cc)：理解线程组装与业务入口。
3. [`src/game/Room.cc`](src/game/Room.cc) 与
   [`src/game/BattleSim.cc`](src/game/BattleSim.cc)：理解 20Hz 权威模拟。
4. [`src/net/EventLoop.cc`](src/net/EventLoop.cc)、
   [`src/net/TcpServer.cc`](src/net/TcpServer.cc) 与
   [`src/net/TcpConnection.cc`](src/net/TcpConnection.cc)：理解 Reactor 实现。
5. [`src/storage/AsyncWriter.cc`](src/storage/AsyncWriter.cc)：理解异步落库边界。
6. [`tests/test_game_e2e.cc`](tests/test_game_e2e.cc) 与
   [`examples/bench_client.cc`](examples/bench_client.cc)：从验证代码反查真实能力。
7. [`client/Assets/Scripts/`](client/Assets/Scripts/)：理解 Unity 网络线程、状态机和渲染闭环。
