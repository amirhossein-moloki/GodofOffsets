using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;

namespace TelegramTradingBot.Core.Domain.Interfaces
{
    public interface IOrderRepository
    {
        Task<OrderOperation?> GetByIdAsync(string id);
        Task<OrderOperation?> GetByIdempotencyKeyAsync(string idempotencyKey);
        Task AddAsync(OrderOperation operation);
        Task UpdateAsync(OrderOperation operation);
        Task<IEnumerable<OrderOperation>> GetIncompleteOperationsAsync(TimeSpan olderThan);
    }
}
