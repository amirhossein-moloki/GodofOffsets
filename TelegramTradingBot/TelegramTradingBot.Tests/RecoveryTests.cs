using System;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Application.Configuration;
using TelegramTradingBot.Core.Application.Services;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Enums;
using TelegramTradingBot.Core.Domain.Interfaces;
using TelegramTradingBot.Infrastructure.Exchange;
using TelegramTradingBot.Infrastructure.Monitoring;
using TelegramTradingBot.Infrastructure.Persistence;
using Xunit;

namespace TelegramTradingBot.Tests
{
    public class RecoveryTests
    {
        private readonly InMemoryDatabase _db;
        private readonly MockExchangeClient _exchange;
        private readonly MonitoringService _monitoring;
        private readonly IdempotencyConfig _config;
        private readonly RecoveryWorker _worker;

        public RecoveryTests()
        {
            _db = new InMemoryDatabase();
            _exchange = new MockExchangeClient();
            _monitoring = new MonitoringService();
            _config = new IdempotencyConfig
            {
                Enabled = true,
                IncompleteOperationTimeoutSeconds = 30, // 30 seconds threshold
                RecoveryIntervalSeconds = 5
            };
            _worker = new RecoveryWorker(_db, _exchange, _monitoring, _config);
        }

        [Fact]
        public async Task RecoveryWorker_StuckSubmittingOrderFoundOnExchange_SuccessfullyResolvesToAuthoritativeState()
        {
            // Arrange
            var opId = Guid.NewGuid().ToString();
            var clientOrderId = "Sig-Rec1:OrderSubmission:Buy";

            var op = new OrderOperation
            {
                Id = opId,
                IdempotencyKey = "Sig-Rec1:OrderSubmission:Buy",
                OperationType = "OrderSubmission",
                Status = OrderState.Submitting,
                ExternalId = clientOrderId,
                CreatedAt = DateTime.UtcNow - TimeSpan.FromSeconds(45), // older than 30s threshold
                UpdatedAt = DateTime.UtcNow - TimeSpan.FromSeconds(45)
            };
            await _db.AddAsync(op);

            // Configure Exchange to have this order as Filled
            _exchange.AddOrderDirectlyToExchange(clientOrderId, "FILLED");

            // Act
            int count = await _worker.RunRecoveryCycleAsync();

            // Assert
            Assert.Equal(1, count);
            Assert.Equal(1, _monitoring.GetMetricValue("RecoveredOperations"));

            var resolvedOp = await ((IOrderRepository)_db).GetByIdAsync(opId);
            Assert.NotNull(resolvedOp);
            Assert.Equal(OrderState.Filled, resolvedOp.Status);
        }

        [Fact]
        public async Task RecoveryWorker_StuckUnknownOrderNotFoundOnExchange_ResolvesToManualInterventionRequired()
        {
            // Arrange
            var opId = Guid.NewGuid().ToString();
            var clientOrderId = "Sig-Rec2:OrderSubmission:Buy";

            var op = new OrderOperation
            {
                Id = opId,
                IdempotencyKey = "Sig-Rec2:OrderSubmission:Buy",
                OperationType = "OrderSubmission",
                Status = OrderState.Unknown,
                ExternalId = clientOrderId,
                CreatedAt = DateTime.UtcNow - TimeSpan.FromSeconds(45), // older than 30s threshold
                UpdatedAt = DateTime.UtcNow - TimeSpan.FromSeconds(45)
            };
            await _db.AddAsync(op);

            // Order is NOT on exchange (it never reached the exchange or was cancelled)

            // Act
            int count = await _worker.RunRecoveryCycleAsync();

            // Assert
            Assert.Equal(0, count); // Not recovered to normal state
            Assert.Equal(1, _monitoring.GetMetricValue("ManualInterventions"));

            var resolvedOp = await ((IOrderRepository)_db).GetByIdAsync(opId);
            Assert.NotNull(resolvedOp);
            Assert.Equal(OrderState.Unknown, resolvedOp.Status); // remains unknown but with manual intervention required emitted

            var events = _monitoring.GetEmittedEvents();
            Assert.Contains(events, e => e.Contains("ManualInterventionRequired"));
        }
    }
}
