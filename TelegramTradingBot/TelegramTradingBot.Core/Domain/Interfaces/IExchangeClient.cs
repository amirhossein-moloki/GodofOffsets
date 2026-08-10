using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;

namespace TelegramTradingBot.Core.Domain.Interfaces
{
    public interface IExchangeClient
    {
        /// <summary>
        /// Submits an order to the exchange. Throws exceptions on timeout or rejection.
        /// </summary>
        Task<string> SubmitOrderAsync(OrderOperation operation);

        /// <summary>
        /// Queries the order state from the exchange using client order id (ExternalId).
        /// Returns (found, state, failureCode).
        /// </summary>
        Task<(bool found, string state, string failureCode)> QueryOrderAsync(string clientOrderId);
    }
}
