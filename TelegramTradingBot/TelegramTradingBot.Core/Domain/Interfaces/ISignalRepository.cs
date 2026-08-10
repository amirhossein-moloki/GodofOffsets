using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;

namespace TelegramTradingBot.Core.Domain.Interfaces
{
    public interface ISignalRepository
    {
        Task<Signal?> GetByIdAsync(string id);
        Task AddAsync(Signal signal);
        Task<bool> ExistsAsync(string id);
    }
}
