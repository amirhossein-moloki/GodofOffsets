namespace TelegramTradingBot.Core.Domain.Enums
{
    public enum RecoveryResult
    {
        Recovered,
        AlreadyCompleted,
        Rejected,
        Cancelled,
        NotFound,
        StillUnknown,
        ManualInterventionRequired
    }
}
