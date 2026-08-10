namespace TelegramTradingBot.Core.Domain.Enums
{
    public enum OrderState
    {
        NotSubmitted = 0,
        Submitting = 1,
        Unknown = 2,
        Submitted = 3,
        Filled = 4,
        Rejected = 5,
        Cancelled = 6,
        Failed = 7
    }
}
