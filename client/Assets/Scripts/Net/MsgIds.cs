namespace GsClient.Net
{
    // 帧头 msgId 对照表——与服务端 src/proto/MsgId.h 严格一致
    public static class MsgIds
    {
        public const ushort Echo = 1;
        public const ushort Heartbeat = 2;
        public const ushort LoginReq = 3;
        public const ushort LoginResp = 4;
        public const ushort ReconnectReq = 5;
        public const ushort MatchReq = 10;
        public const ushort EnterRoom = 11;
        public const ushort PlayerInput = 20;
        public const ushort StateSnapshot = 21;
        public const ushort BattleEnd = 22;
        public const ushort LeaderboardReq = 30;
        public const ushort LeaderboardResp = 31;
    }
}
