using System;

namespace TelegramTradingBot.Core.Domain.Entities
{
    public class Signal
    {
        public string Id { get; set; } = string.Empty; // Unique derived stable ID (e.g., "Source:ChatId:MessageId")
        public string Source { get; set; } = string.Empty;
        public string ChatId { get; set; } = string.Empty;
        public string MessageId { get; set; } = string.Empty;
        public string RawContent { get; set; } = string.Empty;
        public DateTime CreatedAt { get; set; }
    }
}
