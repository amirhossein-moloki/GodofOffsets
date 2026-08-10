using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;

namespace TelegramTradingBot.Core.Domain.Interfaces
{
    public interface IEventRepository
    {
        Task<Event?> GetByIdAsync(string eventId);
        /// <summary>
        /// Attempts to atomically register a new event. If the event already exists, returns false.
        /// </summary>
        Task<bool> TryRegisterEventAsync(Event @event);
        Task MarkProcessedAsync(string eventId, string status);
    }
}
