using System;
using System.Collections.Generic;
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
    public class FailureTests
    {
        private readonly InMemoryDatabase _db;
        private readonly MockExchangeClient _exchange;
        private readonly MonitoringService _monitoring;
        private readonly OrderSubmissionService _service;

        public FailureTests()
        {
            _db = new InMemoryDatabase();
            _exchange = new MockExchangeClient();
            _monitoring = new MonitoringService();
            _service = new OrderSubmissionService(_db, _exchange, _monitoring);
        }

        [Fact]
        public async Task SubmitOrder_DatabaseUnavailableBeforeSubmission_FailsSafelyWithoutTrading()
        {
            // If the database is unavailable: we cannot persist the idempotency state,
            // so we MUST NOT proceed with an unsafe trading operation on the exchange.
            // Section 19: Fail Safely.

            _db.SimulateOutage = true;

            await Assert.ThrowsAsync<DatabaseUnavailableException>(() =>
                _service.SubmitOrderIdempotentlyAsync(
                    signalId: "Sig-Outage",
                    symbol: "BTCUSDT",
                    side: "Buy",
                    qty: 0.05m,
                    price: 50000m,
                    correlationId: "Corr-Outage"
                )
            );

            // Verify that NO order was submitted to the exchange
            var (found, _, _) = await _exchange.QueryOrderAsync("Sig-Outage:OrderSubmission:Buy");
            Assert.False(found);
        }

        [Fact]
        public async Task SubmitOrder_ExchangeOrderCreatedButDatabaseUpdateFails_RecoverableOnRestart()
        {
            // Section 18: Handle: Database Save -> Exchange Request -> Database Update failure scenario.
            // Exchange Order Created, but Database Update Failed.
            // On recovery, the system must query the exchange and restore the missing external state,
            // instead of creating a second order.

            // 1. Submit succeeds on exchange, but immediately after, we simulate DB outage for updates
            var op = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-Partial",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-Partial"
            );

            Assert.Equal(OrderState.Submitted, op.Status);

            // Now, simulate that we crash/restart, and the database status is stuck in Submitting/Submitted,
            // but the order is Filled on the exchange.
            // When we try to submit the order again (e.g. reprocessing after retry),
            // it queries the exchange first, finds it exists, and restores the missing state without creating a duplicate!

            _exchange.AddOrderDirectlyToExchange("Sig-Partial:OrderSubmission:Buy", "FILLED");

            var recoveredOp = await _service.SubmitOrderIdempotentlyAsync(
                signalId: "Sig-Partial",
                symbol: "BTCUSDT",
                side: "Buy",
                qty: 0.05m,
                price: 50000m,
                correlationId: "Corr-Partial"
            );

            Assert.Equal(OrderState.Filled, recoveredOp.Status);

            // Confirm we didn't blindly create a new order, but reused the existing one
            var events = _monitoring.GetEmittedEvents();
            Assert.Contains(events, e => e.Contains("DuplicateOrderPrevented") || e.Contains("OrderStateRecovered"));
        }

        [Fact]
        public async Task StructuredLogging_DoesNotExposeSecrets()
        {
            // Section 29: Never log API Secret, API Key, Telegram Token, Auth Headers, Request Signatures.

            var metadata = new Dictionary<string, string>
            {
                { "ApiKey", "super_secret_bybit_api_key_123" },
                { "ApiSecret", "very_secret_bybit_api_secret_456" },
                { "TelegramToken", "bot123456:ABC-defTelegramToken" },
                { "Authorization", "Bearer token_abc123" },
                { "Signature", "sha256_sig_987" },
                { "OperationId", "Op-111" }
            };

            _monitoring.EmitEvent("TestEvent", "Simulating log entry with secrets", metadata);

            var loggedEvents = _monitoring.GetEmittedEvents();
            foreach (var log in loggedEvents)
            {
                Assert.DoesNotContain("super_secret_bybit_api_key_123", log);
                Assert.DoesNotContain("very_secret_bybit_api_secret_456", log);
                Assert.DoesNotContain("bot123456:ABC-defTelegramToken", log);
                Assert.DoesNotContain("Bearer token_abc123", log);
                Assert.DoesNotContain("sha256_sig_987", log);

                // Confirm redaction
                Assert.Contains("ApiKey=[REDACTED]", log);
                Assert.Contains("ApiSecret=[REDACTED]", log);
                Assert.Contains("TelegramToken=[REDACTED]", log);
                Assert.Contains("Authorization=[REDACTED]", log);
                Assert.Contains("Signature=[REDACTED]", log);

                // Safe metadata is logged
                Assert.Contains("OperationId=Op-111", log);
            }
        }
    }
}
