using System;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Infrastructure.Exchange
{
    public class MockExchangeClient : IExchangeClient
    {
        private readonly ConcurrentDictionary<string, (string State, string FailureCode)> _exchangeOrders = new();

        public bool SimulateTimeout { get; set; } = false;
        public bool SubmitShouldSucceedButTimeoutToClient { get; set; } = false;
        public string DefaultSubmitResultState { get; set; } = "FILLED";
        public string DefaultSubmitResultFailureCode { get; set; } = "";

        public Task<string> SubmitOrderAsync(OrderOperation operation)
        {
            if (SimulateTimeout)
            {
                if (SubmitShouldSucceedButTimeoutToClient)
                {
                    // Order is registered on exchange but client gets a timeout! (Critical Unknown State)
                    _exchangeOrders[operation.ExternalId] = (DefaultSubmitResultState, DefaultSubmitResultFailureCode);
                }
                throw new TimeoutException("Network connection timed out while sending request to Bybit.");
            }

            // Register order on exchange
            _exchangeOrders[operation.ExternalId] = (DefaultSubmitResultState, DefaultSubmitResultFailureCode);
            return Task.FromResult(Guid.NewGuid().ToString());
        }

        public Task<(bool found, string state, string failureCode)> QueryOrderAsync(string clientOrderId)
        {
            if (_exchangeOrders.TryGetValue(clientOrderId, out var order))
            {
                return Task.FromResult((true, order.State, order.FailureCode));
            }
            return Task.FromResult((false, "", ""));
        }

        // Test Helpers
        public void Clear()
        {
            _exchangeOrders.Clear();
            SimulateTimeout = false;
            SubmitShouldSucceedButTimeoutToClient = false;
            DefaultSubmitResultState = "FILLED";
            DefaultSubmitResultFailureCode = "";
        }

        public void AddOrderDirectlyToExchange(string clientOrderId, string state, string failureCode = "")
        {
            _exchangeOrders[clientOrderId] = (state, failureCode);
        }
    }
}
