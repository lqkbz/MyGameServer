using System;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Gs.Pb;
using GsClient.Net;
using NUnit.Framework;

namespace GsClient.Tests
{
    // NetworkClient 单测:进程内 TcpListener 假服务器,不依赖真实游戏服务器
    // EditMode 测试跑在主线程,后台收线程照常工作,Pump() 手动泵消息
    public class NetworkClientTests
    {
        // 轮询等待条件成立(每次轮询前 Pump 主线程队列)
        private static void WaitUntil(NetworkClient cli, Func<bool> cond, int timeoutMs)
        {
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < timeoutMs)
            {
                cli.Pump();
                if (cond()) return;
                Thread.Sleep(10);
            }
        }

        [Test]
        public void Connect_SendEcho_Roundtrip()
        {
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            // 假服务器:echo 原始字节流
            var serverTask = Task.Run(() =>
            {
                using var s = listener.AcceptTcpClient();
                var st = s.GetStream();
                var buf = new byte[4096];
                int n;
                while ((n = st.Read(buf, 0, buf.Length)) > 0) st.Write(buf, 0, n);
            });

            using var cli = new NetworkClient();
            bool connected = false;
            ushort gotId = 0;
            byte[] gotBody = null;
            cli.OnConnected += () => connected = true;
            cli.OnMessage += (id, body) => { gotId = id; gotBody = body; };

            cli.Connect("127.0.0.1", port);
            WaitUntil(cli, () => connected, 3000);
            Assert.IsTrue(connected, "应触发 OnConnected");

            cli.Send(MsgIds.Echo, new EchoMsg { Payload = "ping" });
            WaitUntil(cli, () => gotBody != null, 3000);
            Assert.AreEqual(MsgIds.Echo, gotId);
            Assert.AreEqual("ping", EchoMsg.Parser.ParseFrom(gotBody).Payload);

            cli.Close();
            listener.Stop();
        }

        [Test]
        public void ServerClose_FiresDisconnected()
        {
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            // 假服务器:接受后立即关闭
            var serverTask = Task.Run(() =>
            {
                var s = listener.AcceptTcpClient();
                s.Close();
            });

            using var cli = new NetworkClient();
            bool connected = false;
            string disconnectReason = null;
            cli.OnConnected += () => connected = true;
            cli.OnDisconnected += reason => disconnectReason = reason;

            cli.Connect("127.0.0.1", port);
            WaitUntil(cli, () => disconnectReason != null, 3000);
            Assert.IsTrue(connected, "断开前应先连上");
            Assert.IsNotNull(disconnectReason, "对端关闭应触发 OnDisconnected");
            listener.Stop();
        }

        [Test]
        public void ConnectRefused_FiresDisconnected()
        {
            // 找一个没人监听的端口
            var probe = new TcpListener(IPAddress.Loopback, 0);
            probe.Start();
            int deadPort = ((IPEndPoint)probe.LocalEndpoint).Port;
            probe.Stop(); // 端口已释放,连接必被拒

            using var cli = new NetworkClient();
            string disconnectReason = null;
            cli.OnDisconnected += reason => disconnectReason = reason;

            cli.Connect("127.0.0.1", deadPort);
            WaitUntil(cli, () => disconnectReason != null, 3000);
            Assert.IsNotNull(disconnectReason, "连接被拒应触发 OnDisconnected");
        }
    }
}
