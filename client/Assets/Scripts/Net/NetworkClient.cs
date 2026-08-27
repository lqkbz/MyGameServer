using System;
using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Threading;
using Google.Protobuf;

namespace GsClient.Net
{
    // TCP 网络客户端(与 Unity 解耦,纯 C#):
    // - 后台收线程:阻塞 Read → FrameParser 解帧 → 并发队列
    // - 主线程每帧调 Pump():出队并触发事件回调(Unity API 只能在主线程碰)
    // - Send 线程安全(锁流写);服务端权威模型下发包频率低,无需发送队列
    public class NetworkClient : IDisposable
    {
        public event Action OnConnected;               // Pump 内触发(主线程)
        public event Action<string> OnDisconnected;    // 参数=原因
        public event Action<ushort, byte[]> OnMessage; // (msgId, pb body)

        private enum EvKind { Connected, Disconnected, Message }

        private struct Ev
        {
            public EvKind Kind;
            public ushort MsgId;
            public byte[] Body;
            public string Reason;
        }

        private readonly ConcurrentQueue<Ev> queue_ = new ConcurrentQueue<Ev>();
        private readonly object sendLock_ = new object();
        private TcpClient tcp_;
        private NetworkStream stream_;
        private Thread recvThread_;
        private volatile bool closed_;

        public bool IsConnected { get; private set; }

        // 异步连接:连接与收包都在后台线程,结果经队列回主线程
        public void Connect(string host, int port)
        {
            closed_ = false;
            recvThread_ = new Thread(() => RecvLoop(host, port)) { IsBackground = true, Name = "gs-recv" };
            recvThread_.Start();
        }

        private void RecvLoop(string host, int port)
        {
            try
            {
                tcp_ = new TcpClient();
                tcp_.NoDelay = true; // 小包低延迟,关 Nagle(与服务端一致)
                tcp_.Connect(host, port);
                stream_ = tcp_.GetStream();
                queue_.Enqueue(new Ev { Kind = EvKind.Connected });
            }
            catch (Exception e)
            {
                queue_.Enqueue(new Ev { Kind = EvKind.Disconnected, Reason = $"connect failed: {e.Message}" });
                return;
            }

            var parser = new FrameParser();
            var buf = new byte[8 * 1024];
            try
            {
                while (!closed_)
                {
                    int n = stream_.Read(buf, 0, buf.Length);
                    if (n <= 0) // 对端关闭
                    {
                        queue_.Enqueue(new Ev { Kind = EvKind.Disconnected, Reason = "peer closed" });
                        return;
                    }
                    parser.Append(buf, n);
                    while (parser.TryTake(out ushort id, out byte[] body))
                        queue_.Enqueue(new Ev { Kind = EvKind.Message, MsgId = id, Body = body });
                    if (parser.Corrupt)
                    {
                        queue_.Enqueue(new Ev { Kind = EvKind.Disconnected, Reason = "corrupt frame" });
                        return;
                    }
                }
            }
            catch (Exception e)
            {
                if (!closed_)
                    queue_.Enqueue(new Ev { Kind = EvKind.Disconnected, Reason = $"recv error: {e.Message}" });
            }
        }

        // 线程安全发送;未连接时静默丢弃(上层状态机保证时序)
        public void Send(ushort msgId, IMessage msg)
        {
            var s = stream_;
            if (s == null || closed_) return;
            byte[] frame = ProtoCodec.Encode(msgId, msg);
            try
            {
                lock (sendLock_) s.Write(frame, 0, frame.Length);
            }
            catch (Exception)
            {
                // 写失败交给收线程报断开,这里不重复上报
            }
        }

        // 主线程每帧调用:泵出网络事件并触发回调
        public void Pump(int maxEvents = 64)
        {
            for (int i = 0; i < maxEvents && queue_.TryDequeue(out Ev ev); i++)
            {
                switch (ev.Kind)
                {
                    case EvKind.Connected:
                        IsConnected = true;
                        OnConnected?.Invoke();
                        break;
                    case EvKind.Disconnected:
                        IsConnected = false;
                        OnDisconnected?.Invoke(ev.Reason);
                        break;
                    case EvKind.Message:
                        OnMessage?.Invoke(ev.MsgId, ev.Body);
                        break;
                }
            }
        }

        public void Close()
        {
            closed_ = true;
            IsConnected = false;
            try { tcp_?.Close(); } catch (Exception) { }
        }

        public void Dispose() => Close();
    }
}
