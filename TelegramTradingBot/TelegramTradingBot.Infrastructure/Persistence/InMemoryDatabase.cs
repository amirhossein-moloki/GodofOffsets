using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Enums;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Infrastructure.Persistence
{
    public class DuplicateKeyException : Exception
    {
        public DuplicateKeyException(string message) : base(message) { }
    }

    public class DatabaseUnavailableException : Exception
    {
        public DatabaseUnavailableException(string message) : base(message) { }
    }

    public class InMemoryDatabase : ISignalRepository, IEventRepository, IOrderRepository
    {
        private readonly ConcurrentDictionary<string, Signal> _signals = new();
        private readonly ConcurrentDictionary<string, Event> _events = new();
        private readonly ConcurrentDictionary<string, OrderOperation> _orders = new();

        private readonly object _lock = new();

        public bool SimulateOutage { get; set; } = false;

        private void CheckDatabaseAvailability()
        {
            if (SimulateOutage)
            {
                throw new DatabaseUnavailableException("Database connection timed out or unavailable.");
            }
        }

        // --- ISignalRepository ---
        public Task<Signal?> GetByIdAsync(string id)
        {
            CheckDatabaseAvailability();
            _signals.TryGetValue(id, out var signal);
            return Task.FromResult(signal);
        }

        public Task AddAsync(Signal signal)
        {
            CheckDatabaseAvailability();
            lock (_lock)
            {
                if (_signals.ContainsKey(signal.Id))
                {
                    throw new DuplicateKeyException($"UniqueConstraint violation on Signal.Id: {signal.Id}");
                }
                _signals[signal.Id] = signal;
            }
            return Task.CompletedTask;
        }

        public Task<bool> ExistsAsync(string id)
        {
            CheckDatabaseAvailability();
            return Task.FromResult(_signals.ContainsKey(id));
        }

        // --- IEventRepository ---
        Task<Event?> IEventRepository.GetByIdAsync(string eventId)
        {
            CheckDatabaseAvailability();
            _events.TryGetValue(eventId, out var @event);
            return Task.FromResult(@event);
        }

        public Task<bool> TryRegisterEventAsync(Event @event)
        {
            CheckDatabaseAvailability();
            lock (_lock)
            {
                if (_events.ContainsKey(@event.EventId))
                {
                    return Task.FromResult(false); // already registered
                }
                @event.ProcessedAt = null;
                @event.Status = "New";
                _events[@event.EventId] = @event;
                return Task.FromResult(true); // newly registered
            }
        }

        public Task MarkProcessedAsync(string eventId, string status)
        {
            CheckDatabaseAvailability();
            lock (_lock)
            {
                if (_events.TryGetValue(eventId, out var @event))
                {
                    @event.Status = status;
                    @event.ProcessedAt = DateTime.UtcNow;
                }
            }
            return Task.CompletedTask;
        }

        // --- IOrderRepository ---
        Task<OrderOperation?> IOrderRepository.GetByIdAsync(string id)
        {
            CheckDatabaseAvailability();
            _orders.TryGetValue(id, out var op);
            return Task.FromResult(op);
        }

        public Task<OrderOperation?> GetByIdempotencyKeyAsync(string idempotencyKey)
        {
            CheckDatabaseAvailability();
            var op = _orders.Values.FirstOrDefault(o => o.IdempotencyKey == idempotencyKey);
            return Task.FromResult(op);
        }

        public Task AddAsync(OrderOperation operation)
        {
            CheckDatabaseAvailability();
            lock (_lock)
            {
                if (_orders.Values.Any(o => o.IdempotencyKey == operation.IdempotencyKey))
                {
                    throw new DuplicateKeyException($"UniqueConstraint violation on OrderOperation.IdempotencyKey: {operation.IdempotencyKey}");
                }
                _orders[operation.Id] = operation;
            }
            return Task.CompletedTask;
        }

        public Task UpdateAsync(OrderOperation operation)
        {
            CheckDatabaseAvailability();
            lock (_lock)
            {
                if (_orders.ContainsKey(operation.Id))
                {
                    _orders[operation.Id] = operation;
                }
            }
            return Task.CompletedTask;
        }

        public Task<IEnumerable<OrderOperation>> GetIncompleteOperationsAsync(TimeSpan olderThan)
        {
            CheckDatabaseAvailability();
            var cutoff = DateTime.UtcNow - olderThan;
            var incompleteStates = new[] { OrderState.Submitting, OrderState.Unknown };

            var result = _orders.Values
                .Where(o => incompleteStates.Contains(o.Status) && o.CreatedAt <= cutoff)
                .ToList();

            return Task.FromResult<IEnumerable<OrderOperation>>(result);
        }

        // --- Test Helpers ---
        public void Clear()
        {
            _signals.Clear();
            _events.Clear();
            _orders.Clear();
            SimulateOutage = false;
        }
    }
}
