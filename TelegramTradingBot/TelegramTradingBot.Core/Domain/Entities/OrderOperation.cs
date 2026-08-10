using System;
using TelegramTradingBot.Core.Domain.Enums;

namespace TelegramTradingBot.Core.Domain.Entities
{
    public class OrderOperation
    {
        public string Id { get; set; } = string.Empty; // Internal ID (e.g. Guid)
        public string IdempotencyKey { get; set; } = string.Empty; // Deterministic Key: SignalId + OperationType + Side
        public string OperationType { get; set; } = string.Empty; // e.g. "OrderSubmission"
        public string CorrelationId { get; set; } = string.Empty;
        public OrderState Status { get; set; } = OrderState.NotSubmitted;
        public string ExternalId { get; set; } = string.Empty; // Exchange order ID or Client Order ID
        public string Symbol { get; set; } = string.Empty;
        public string Side { get; set; } = string.Empty; // "Buy" or "Sell"
        public decimal Qty { get; set; }
        public decimal? Price { get; set; }
        public string FailureCode { get; set; } = string.Empty;
        public DateTime CreatedAt { get; set; }
        public DateTime UpdatedAt { get; set; }
        public DateTime? CompletedAt { get; set; }
    }
}
