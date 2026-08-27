using Gs.Pb;
using GsClient.Net;
using NUnit.Framework;

namespace GsClient.Tests
{
    // ProtoCodec 单测:与服务端帧协议逐字节对齐
    // 帧格式: 4B 大端长度(=2+body) | 2B 大端 msgId | protobuf body,上限 64KB
    public class ProtoCodecTests
    {
        [Test]
        public void Encode_WireFormat_BigEndian()
        {
            var echo = new EchoMsg { Payload = "hi" };
            byte[] body = Google.Protobuf.MessageExtensions.ToByteArray(echo);
            byte[] frame = ProtoCodec.Encode(MsgIds.Echo, echo);

            Assert.AreEqual(4 + 2 + body.Length, frame.Length);
            // 长度字段 = 2(msgId) + body,大端
            int len = (frame[0] << 24) | (frame[1] << 16) | (frame[2] << 8) | frame[3];
            Assert.AreEqual(2 + body.Length, len);
            // msgId 大端
            int msgId = (frame[4] << 8) | frame[5];
            Assert.AreEqual((int)MsgIds.Echo, msgId);
        }

        [Test]
        public void Roundtrip_SingleFrame()
        {
            var parser = new FrameParser();
            byte[] frame = ProtoCodec.Encode(MsgIds.Echo, new EchoMsg { Payload = "hello" });
            parser.Append(frame, frame.Length);

            Assert.IsTrue(parser.TryTake(out ushort id, out byte[] body));
            Assert.AreEqual(MsgIds.Echo, id);
            Assert.AreEqual("hello", EchoMsg.Parser.ParseFrom(body).Payload);
            Assert.IsFalse(parser.TryTake(out _, out _)); // 无残留
        }

        [Test]
        public void Sticky_TwoFramesOneAppend()
        {
            var parser = new FrameParser();
            byte[] f1 = ProtoCodec.Encode(MsgIds.Echo, new EchoMsg { Payload = "a" });
            byte[] f2 = ProtoCodec.Encode(MsgIds.Heartbeat, new Heartbeat { ClientMs = 42 });
            byte[] both = new byte[f1.Length + f2.Length];
            f1.CopyTo(both, 0);
            f2.CopyTo(both, f1.Length);
            parser.Append(both, both.Length);

            Assert.IsTrue(parser.TryTake(out ushort id1, out byte[] b1));
            Assert.AreEqual(MsgIds.Echo, id1);
            Assert.AreEqual("a", EchoMsg.Parser.ParseFrom(b1).Payload);
            Assert.IsTrue(parser.TryTake(out ushort id2, out byte[] b2));
            Assert.AreEqual(MsgIds.Heartbeat, id2);
            Assert.AreEqual(42, Heartbeat.Parser.ParseFrom(b2).ClientMs);
        }

        [Test]
        public void Half_SplitAcrossAppends()
        {
            var parser = new FrameParser();
            byte[] frame = ProtoCodec.Encode(MsgIds.Echo, new EchoMsg { Payload = "half-packet" });

            // 头部中间截断
            parser.Append(frame, 2);
            Assert.IsFalse(parser.TryTake(out _, out _));
            // body 中间截断
            byte[] mid = new byte[5];
            System.Array.Copy(frame, 2, mid, 0, 5);
            parser.Append(mid, 5);
            Assert.IsFalse(parser.TryTake(out _, out _));
            // 余下全部
            byte[] rest = new byte[frame.Length - 7];
            System.Array.Copy(frame, 7, rest, 0, rest.Length);
            parser.Append(rest, rest.Length);
            Assert.IsTrue(parser.TryTake(out ushort id, out byte[] body));
            Assert.AreEqual("half-packet", EchoMsg.Parser.ParseFrom(body).Payload);
        }

        [Test]
        public void EmptyBody_Frame()
        {
            // 默认 EchoMsg 序列化为 0 字节 body → len 字段 = 2
            var parser = new FrameParser();
            byte[] frame = ProtoCodec.Encode(MsgIds.Echo, new EchoMsg());
            Assert.AreEqual(6, frame.Length);
            parser.Append(frame, frame.Length);
            Assert.IsTrue(parser.TryTake(out ushort id, out byte[] body));
            Assert.AreEqual(0, body.Length);
        }

        [Test]
        public void Oversize_SetsCorrupt()
        {
            // 手工构造 len=70000(>64KB)的非法帧头
            var parser = new FrameParser();
            int bad = 70000;
            byte[] header = {
                (byte)(bad >> 24), (byte)(bad >> 16), (byte)(bad >> 8), (byte)bad,
                0, 1,
            };
            parser.Append(header, header.Length);
            Assert.IsFalse(parser.TryTake(out _, out _));
            Assert.IsTrue(parser.Corrupt);
        }
    }
}
