using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Core.Application.Services
{
    public class EventProcessor
    {
        private readonly IEventRepository _eventRepository;
        private readonly IMonitoringService _monitoringService;

        public EventProcessor(IEventRepository eventRepository, IMonitoringService monitoringService)
        {
            _eventRepository = eventRepository;
            _monitoringService = monitoringService;
        }

        /// <summary>
        /// Processes an event idempotently.
        /// Only the first logical event triggers the handler.
        /// </summary>
        public async Task<bool> ProcessEventAsync(Event @event, Func<Event, Task> handler)
        {
            if (string.IsNullOrWhiteSpace(@event.EventId))
            {
                throw new ArgumentException("EventId must not be empty.", nameof(@event.EventId));
            }

            // Attempt Atomic Registration
            bool isNew = await _eventRepository.TryRegisterEventAsync(@event);
            if (!isNew)
            {
                // Duplicate Event -> Stop / Ignore
                _monitoringService.IncrementMetric("DuplicateEvents");
                _monitoringService.EmitEvent("DuplicateEventDetected", $"Duplicate event ignored: {@event.EventId}",
                    new Dictionary<string, string> { { "EventId", @event.EventId }, { "EventType", @event.EventType } });
                return false;
            }

            // New event -> Process Event
            try
            {
                await handler(@event);

                // Mark Processed
                await _eventRepository.MarkProcessedAsync(@event.EventId, "Processed");

                _monitoringService.EmitEvent("EventProcessed", $"Successfully handled event: {@event.EventId}",
                    new Dictionary<string, string> { { "EventId", @event.EventId } });

                return true;
            }
            catch (Exception ex)
            {
                // Process failed -> Mark as Failed so it can be retried or inspected
                await _eventRepository.MarkProcessedAsync(@event.EventId, "Failed");

                _monitoringService.EmitEvent("EventProcessingFailed", $"Failed handling event {@event.EventId}: {ex.Message}",
                    new Dictionary<string, string> { { "EventId", @event.EventId }, { "Error", ex.Message } });

                throw; // Rethrow to let caller handle
            }
        }
    }
}
