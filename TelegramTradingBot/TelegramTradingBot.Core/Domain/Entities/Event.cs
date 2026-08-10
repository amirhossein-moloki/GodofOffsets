using System;

namespace TelegramTradingBot.Core.Domain.Entities
{
    public class Event
    {
        public string EventId { get; set; } = string.Empty; // Unique immutable ID (e.g., execution ID or OrderId+Type)
        public string EventType { get; set; } = string.Empty; // e.g. "OrderFilled"
        public string Source { get; set; } = string.Empty;
        public DateTime OccurredAt { get; set; }
        public string CorrelationId { get; set; } = string.Empty;
        public DateTime? ProcessedAt { get; set; }
        public string Status { get; set; } = "New"; // "New", "Processed", "Failed"
    }
}
