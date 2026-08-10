using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Application.Services;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Interfaces;
using TelegramTradingBot.Infrastructure.Monitoring;
using TelegramTradingBot.Infrastructure.Persistence;
using Xunit;

namespace TelegramTradingBot.Tests
{
    public class SignalIdempotencyTests
    {
        private readonly InMemoryDatabase _db;
        private readonly MonitoringService _monitoring;
        private readonly SignalProcessor _processor;

        public SignalIdempotencyTests()
        {
            _db = new InMemoryDatabase();
            _monitoring = new MonitoringService();
            _processor = new SignalProcessor(_db, _monitoring);
        }

        private class MockUniqueViolationSignalRepository : ISignalRepository
        {
            public Task<Signal?> GetByIdAsync(string id) => Task.FromResult<Signal?>(null);
            public Task<bool> ExistsAsync(string id) => Task.FromResult(false); // bypass exists check
            public Task AddAsync(Signal signal) => throw new DuplicateKeyException("UniqueConstraint violation.");
        }

        [Fact]
        public async Task ProcessSignal_FirstSignal_SuccessfullyProcessed()
        {
            var signal = new Signal
            {
                Source = "TelegramChannel1",
                ChatId = "12345",
                MessageId = "9876",
                RawContent = "BUY BTCUSDT Qty=0.1"
            };

            bool result = await _processor.ProcessSignalAsync(signal);

            Assert.True(result);
            Assert.Equal(0, _monitoring.GetMetricValue("DuplicateSignals"));

            // Check that it was persisted
            var persisted = await _db.GetByIdAsync("TelegramChannel1:12345:9876");
            Assert.NotNull(persisted);
            Assert.Equal("BUY BTCUSDT Qty=0.1", persisted.RawContent);
        }

        [Fact]
        public async Task ProcessSignal_DuplicateSignal_IgnoredAndMetricsIncremented()
        {
            var signal1 = new Signal
            {
                Id = "UniqueSignalId",
                Source = "TelegramChannel1",
                ChatId = "12345",
                MessageId = "9876",
                RawContent = "BUY BTCUSDT Qty=0.1"
            };

            var signal2 = new Signal
            {
                Id = "UniqueSignalId",
                Source = "TelegramChannel1",
                ChatId = "12345",
                MessageId = "9876",
                RawContent = "BUY BTCUSDT Qty=0.1"
            };

            bool result1 = await _processor.ProcessSignalAsync(signal1);
            bool result2 = await _processor.ProcessSignalAsync(signal2);

            Assert.True(result1);
            Assert.False(result2); // Ignored

            Assert.Equal(1, _monitoring.GetMetricValue("DuplicateSignals"));

            // Verify event logged
            var events = _monitoring.GetEmittedEvents();
            Assert.Contains(events, e => e.Contains("DuplicateSignalDetected"));
        }

        [Fact]
        public async Task ProcessSignal_DbUniqueConstraintViolation_HandledSafely()
        {
            var signal = new Signal
            {
                Id = "UniqueDbConstraintSignal",
                Source = "TG",
                ChatId = "123",
                MessageId = "456",
                RawContent = "SELL BTCUSDT"
            };

            var mockRepo = new MockUniqueViolationSignalRepository();
            var processorWithMockRepo = new SignalProcessor(mockRepo, _monitoring);

            // When the processor tries to call AddAsync, it will throw DuplicateKeyException (representing UniqueConstraint).
            // Processor should catch it, register it as DuplicateSignalDetected, and return false safely.
            bool result = await processorWithMockRepo.ProcessSignalAsync(signal);

            Assert.False(result);
            Assert.Equal(1, _monitoring.GetMetricValue("DuplicateSignals"));

            var events = _monitoring.GetEmittedEvents();
            Assert.Contains(events, e => e.Contains("DuplicateSignalDetected") && e.Contains("UniqueConstraintViolation"));
        }

        [Fact]
        public async Task ProcessSignal_ConcurrentDuplicateSignals_OnlyOneSucceedsAndDuplicateDetected()
        {
            // Simulate Section 8 Race Condition:
            // Two parallel workers attempt to process the exact same signal concurrently.

            var signalId = "RaceConditionSignalId";
            var signal1 = new Signal { Id = signalId, Source = "TG", ChatId = "1", MessageId = "1", RawContent = "BUY" };
            var signal2 = new Signal { Id = signalId, Source = "TG", ChatId = "1", MessageId = "1", RawContent = "BUY" };

            var task1 = Task.Run(() => _processor.ProcessSignalAsync(signal1));
            var task2 = Task.Run(() => _processor.ProcessSignalAsync(signal2));

            bool[] results = await Task.WhenAll(task1, task2);

            // One MUST succeed, and the other MUST fail safely and return false
            Assert.True(results[0] ^ results[1]); // XOR: exactly one is true

            Assert.Equal(1, _monitoring.GetMetricValue("DuplicateSignals"));

            var events = _monitoring.GetEmittedEvents();
            Assert.Contains(events, e => e.Contains("DuplicateSignalDetected"));
        }
    }
}
