# Unity 客户端(M6)

2D 俯视角双人对战客户端,与本仓库 C++ 服务器联机(TCP + protobuf,帧协议见 src/proto)。

## 本目录是什么

Unity 工程实际位于本机 `F:\unitypj\gs-client\gsclient`(Unity 6000.3.11f1, Universal 2D 模板)。
这里归档其中**手写部分**:`Assets/Scripts`(运行时代码)与 `Assets/Tests`(EditMode 测试),
Unity 生成的 Library/Temp 等不入库。

## 从零复原工程

1. Unity Hub 新建 Universal 2D 工程(Unity 6000.x)
2. 把本目录 `Assets/` 内容拷入工程 `Assets/`
3. NuGet 下载 `Google.Protobuf 3.21.12`,取 `lib/netstandard2.0/Google.Protobuf.dll` 放 `Assets/Plugins/`
4. 生成协议代码(与服务端同版 protoc 3.21.12):
   `bash scripts/gen_csharp.sh <工程>/Assets/Scripts/Proto`
   (Game.cs 为生成物,不入库)
5. 服务器跑在 WSL:`./build/game_server 9100 4 4`,客户端连 127.0.0.1:9100

## 操作与子弹同步

- `W/A/S/D` 移动，`Space` 发射；双开时由当前获得焦点的窗口接收键盘输入。
- 攻击会在服务器生成唯一子弹 ID；服务器以 20Hz 推进弹道、执行线段碰撞与伤害结算，并在 `StateSnapshot` 中广播存活子弹和销毁 tombstone。
- Unity 根据权威位置动态创建发光弹体、青色/橙色拖尾与命中闪光；本机子弹显示为青色，对手子弹显示为橙色，两个客户端看到的是同一颗服务器子弹。

## 程序集划分

- `GsClient.Runtime`(Assets/Scripts):协议编解码、网络线程、游戏状态机、快照插值、渲染与 UI
- `GsClient.Tests`(Assets/Tests, EditMode):纯逻辑单元测试,不依赖 Play 模式
