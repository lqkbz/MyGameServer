using System;
using Google.Protobuf;

namespace GsClient.Net
{
    // 帧编解码:与服务端 ProtobufCodec 逐字节对齐
    // 帧格式: 4B 大端长度(=2+body) | 2B 大端 msgId | protobuf body
    public static class ProtoCodec
    {
        public const int HeaderLen = 4;          // 长度字段本身
        public const int MsgIdLen = 2;
        public const int MaxFrameLen = 64 * 1024; // 超过视为非法帧(与服务端一致)

        public static byte[] Encode(ushort msgId, IMessage msg)
        {
            byte[] body = msg.ToByteArray();
            int len = MsgIdLen + body.Length; // 长度字段不含自身
            byte[] frame = new byte[HeaderLen + len];
            frame[0] = (byte)(len >> 24);
            frame[1] = (byte)(len >> 16);
            frame[2] = (byte)(len >> 8);
            frame[3] = (byte)len;
            frame[4] = (byte)(msgId >> 8);
            frame[5] = (byte)msgId;
            body.CopyTo(frame, HeaderLen + MsgIdLen);
            return frame;
        }
    }

    // 收流分帧器:处理 TCP 粘包/半包。非线程安全,由收线程独占使用
    public class FrameParser
    {
        private byte[] buf_ = new byte[8 * 1024];
        private int size_; // buf_[0..size_) 为有效数据

        // 收到非法帧(长度越界)后置位;上层应断开连接
        public bool Corrupt { get; private set; }

        public void Append(byte[] data, int len)
        {
            if (Corrupt) return;
            if (size_ + len > buf_.Length)
            {
                int cap = buf_.Length * 2;
                while (cap < size_ + len) cap *= 2;
                Array.Resize(ref buf_, cap);
            }
            Array.Copy(data, 0, buf_, size_, len);
            size_ += len;
        }

        // 尝试取出一个完整帧;返回 false 表示数据不足(或已损坏)
        public bool TryTake(out ushort msgId, out byte[] body)
        {
            msgId = 0;
            body = null;
            if (Corrupt || size_ < ProtoCodec.HeaderLen) return false;

            int len = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
            if (len < ProtoCodec.MsgIdLen || len > ProtoCodec.MaxFrameLen)
            {
                Corrupt = true; // 与服务端一致:非法帧即视为流损坏
                return false;
            }
            if (size_ < ProtoCodec.HeaderLen + len) return false; // 半包,等更多数据

            msgId = (ushort)((buf_[4] << 8) | buf_[5]);
            int bodyLen = len - ProtoCodec.MsgIdLen;
            body = new byte[bodyLen];
            Array.Copy(buf_, ProtoCodec.HeaderLen + ProtoCodec.MsgIdLen, body, 0, bodyLen);

            // 前移剩余数据(帧小且频率低,搬移成本可忽略)
            int consumed = ProtoCodec.HeaderLen + len;
            Array.Copy(buf_, consumed, buf_, 0, size_ - consumed);
            size_ -= consumed;
            return true;
        }
    }
}
