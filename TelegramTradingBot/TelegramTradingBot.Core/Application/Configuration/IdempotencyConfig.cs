namespace TelegramTradingBot.Core.Application.Configuration
{
    public class IdempotencyConfig
    {
        public bool Enabled { get; set; } = true;
        public int IncompleteOperationTimeoutSeconds { get; set; } = 60;
        public int RecoveryIntervalSeconds { get; set; } = 10;
        public int EventRetentionDays { get; set; } = 7;
    }
}
