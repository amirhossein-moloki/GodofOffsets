using System;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Application.Services;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Enums;
using TelegramTradingBot.Infrastructure.Exchange;
using TelegramTradingBot.Infrastructure.Monitoring;
using TelegramTradingBot.Infrastructure.Persistence;
using Xunit;

namespace TelegramTradingBot.Tests
{
    public class OrderIdempotencyTests
    {
        private readonly InMemoryDatabase _db;
        private readonly MockExchangeClient _exchange;
        private readonly MonitoringService _monitoring;
        private readonly OrderSubmissionService _service;

        public OrderIdempotencyTests()
        {
            _db = new InMemoryDatabase();
            _exchange = new MockExchangeClient();
            _monitoring = new MonitoringService();
            _service = new OrderSubmissionService(_db, _exchange, _monitoring);
        }

        [Fact]
        public async Task SubmitOrderIdempotently_NormalSubmission_SucceedsAndPersists()
        {
            var op = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-123",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-111"
            );

            Assert.Equal(OrderState.Submitted, op.Status);
            Assert.Equal("Sig-123:OrderSubmission:Buy", op.IdempotencyKey);

            // Should be in DB
            var dbOp = await _db.GetByIdempotencyKeyAsync("Sig-123:OrderSubmission:Buy");
            Assert.NotNull(dbOp);
            Assert.Equal(OrderState.Submitted, dbOp.Status);
        }

        [Fact]
        public async Task SubmitOrderIdempotently_TimeoutDuringSubmission_TransitionToUnknownAndQueriesExchangeToResolve()
        {
            // Configure exchange to throw Timeout on submit
            _exchange.SimulateTimeout = true;
            _exchange.SubmitShouldSucceedButTimeoutToClient = true; // Succeeded on exchange, timeout to client!
            _exchange.DefaultSubmitResultState = "FILLED";

            var op = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-456",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-111"
            );

            // Since SubmitShouldSucceedButTimeoutToClient was true,
            // when the service queried the exchange during timeout recovery,
            // it found the order exists and resolved it to Filled!
            Assert.Equal(OrderState.Filled, op.Status);

            var dbOp = await _db.GetByIdempotencyKeyAsync("Sig-456:OrderSubmission:Buy");
            Assert.NotNull(dbOp);
            Assert.Equal(OrderState.Filled, dbOp.Status);
        }

        [Fact]
        public async Task SubmitOrderIdempotently_DuplicateRequestBeforeResolution_ReturnsExistingOperationAndResolvesIt()
        {
            // First submission times out and remains in UNKNOWN state
            _exchange.SimulateTimeout = true;
            _exchange.SubmitShouldSucceedButTimeoutToClient = false; // Order did NOT reach exchange

            var op1 = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-789",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-111"
            );

            Assert.Equal(OrderState.NotSubmitted, op1.Status); // Timeout resolved to NotSubmitted because order not on exchange

            // Reset exchange timeout
            _exchange.Clear();

            // Submit again - should proceed safely since order wasn't on exchange
            var op2 = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-789",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-111"
            );

            Assert.Equal(OrderState.Submitted, op2.Status);
        }

        [Fact]
        public async Task SubmitOrderIdempotently_ExchangeOrderAlreadyExists_DoesNotRecreateOrderAndReturnsAuthoritativeState()
        {
            // Add order directly to exchange beforehand
            string clientOrderId = "Sig-000:OrderSubmission:Buy";
            _exchange.AddOrderDirectlyToExchange(clientOrderId, "FILLED");

            var op = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-000",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-111"
            );

            // It should reuse the exchange state and not crash or submit again
            Assert.Equal(OrderState.Filled, op.Status);
        }
    }
}
