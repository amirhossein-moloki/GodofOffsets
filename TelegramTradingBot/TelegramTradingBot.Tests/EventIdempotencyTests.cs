using System;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Application.Services;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Infrastructure.Monitoring;
using TelegramTradingBot.Infrastructure.Persistence;
using Xunit;

namespace TelegramTradingBot.Tests
{
    public class EventIdempotencyTests
    {
        private readonly InMemoryDatabase _db;
        private readonly MonitoringService _monitoring;
        private readonly EventProcessor _processor;

        public EventIdempotencyTests()
        {
            _db = new InMemoryDatabase();
            _monitoring = new MonitoringService();
            _processor = new EventProcessor(_db, _monitoring);
        }

        [Fact]
        public async Task ProcessEvent_FirstEvent_SuccessfullyExecutesHandler()
        {
            var @event = new Event
            {
                EventId = "Exec-10001",
                EventType = "OrderFilled",
                Source = "Bybit",
                OccurredAt = DateTime.UtcNow,
                CorrelationId = "Corr-111"
            };

            int executionCount = 0;
            Func<Event, Task> handler = (ev) =>
            {
                executionCount++;
                return Task.CompletedTask;
            };

            bool processed = await _processor.ProcessEventAsync(@event, handler);

            Assert.True(processed);
            Assert.Equal(1, executionCount);
            Assert.Equal(0, _monitoring.GetMetricValue("DuplicateEvents"));
        }

        [Fact]
        public async Task ProcessEvent_DuplicateEvent_HandlerNotExecutedAndEventIgnored()
        {
            var eventId = "Exec-10001";
            var event1 = new Event { EventId = eventId, EventType = "OrderFilled", Source = "Bybit", OccurredAt = DateTime.UtcNow };
            var event2 = new Event { EventId = eventId, EventType = "OrderFilled", Source = "Bybit", OccurredAt = DateTime.UtcNow };

            int executionCount = 0;
            Func<Event, Task> handler = (ev) =>
            {
                executionCount++;
                return Task.CompletedTask;
            };

            bool processed1 = await _processor.ProcessEventAsync(event1, handler);
            bool processed2 = await _processor.ProcessEventAsync(event2, handler);

            Assert.True(processed1);
            Assert.False(processed2); // Ignored
            Assert.Equal(1, executionCount); // Handler only runs once!

            Assert.Equal(1, _monitoring.GetMetricValue("DuplicateEvents"));
        }

        [Fact]
        public async Task ProcessEvent_ConcurrentDuplicateEvents_OnlyOneHandlerExecutes()
        {
            var eventId = "Exec-Concurrent-111";
            var event1 = new Event { EventId = eventId, EventType = "OrderFilled" };
            var event2 = new Event { EventId = eventId, EventType = "OrderFilled" };

            int executionCount = 0;
            Func<Event, Task> handler = async (ev) =>
            {
                await Task.Delay(10); // simulate work
                executionCount++;
            };

            var task1 = Task.Run(() => _processor.ProcessEventAsync(event1, handler));
            var task2 = Task.Run(() => _processor.ProcessEventAsync(event2, handler));

            bool[] results = await Task.WhenAll(task1, task2);

            Assert.True(results[0] ^ results[1]); // only one of them processed
            Assert.Equal(1, executionCount); // handler executed exactly once
        }
    }
}
