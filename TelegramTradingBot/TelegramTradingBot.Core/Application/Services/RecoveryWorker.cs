using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Application.Configuration;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Enums;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Core.Application.Services
{
    public class RecoveryWorker
    {
        private readonly IOrderRepository _orderRepository;
        private readonly IExchangeClient _exchangeClient;
        private readonly IMonitoringService _monitoringService;
        private readonly IdempotencyConfig _config;

        public RecoveryWorker(
            IOrderRepository orderRepository,
            IExchangeClient exchangeClient,
            IMonitoringService monitoringService,
            IdempotencyConfig config)
        {
            _orderRepository = orderRepository;
            _exchangeClient = exchangeClient;
            _monitoringService = monitoringService;
            _config = config;
        }

        /// <summary>
        /// Finds and resolves any incomplete operations (stuck in Submitting/Unknown/Processing states).
        /// </summary>
        public async Task<int> RunRecoveryCycleAsync()
        {
            if (!_config.Enabled)
            {
                return 0;
            }

            var timeoutThreshold = TimeSpan.FromSeconds(_config.IncompleteOperationTimeoutSeconds);
            var incompleteOps = await _orderRepository.GetIncompleteOperationsAsync(timeoutThreshold);

            int recoveredCount = 0;

            foreach (var op in incompleteOps)
            {
                _monitoringService.IncrementMetric("IncompleteOperationsDetected");
                _monitoringService.EmitEvent("IncompleteOperationDetected", $"Incomplete operation {op.Id} in state {op.Status} older than threshold.",
                    new Dictionary<string, string> { { "OperationId", op.Id }, { "Status", op.Status.ToString() } });

                try
                {
                    _monitoringService.EmitEvent("OperationRecoveryStarted", $"Recovery worker resolving {op.Id}...");

                    // Treat Exchange state as Authoritative for exchange-created orders
                    var (found, exchangeState, failureCode) = await _exchangeClient.QueryOrderAsync(op.ExternalId);

                    if (found)
                    {
                        op.Status = ParseExchangeState(exchangeState);
                        op.FailureCode = failureCode;
                        op.UpdatedAt = DateTime.UtcNow;
                        if (op.Status == OrderState.Filled || op.Status == OrderState.Rejected || op.Status == OrderState.Cancelled || op.Status == OrderState.Failed)
                        {
                            op.CompletedAt = DateTime.UtcNow;
                        }

                        await _orderRepository.UpdateAsync(op);

                        _monitoringService.IncrementMetric("RecoveredOperations");
                        _monitoringService.EmitEvent("OperationRecovered", $"Operation {op.Id} successfully resolved to {op.Status} based on authoritative Exchange state.");
                        recoveredCount++;
                    }
                    else
                    {
                        // Order is not found on exchange.
                        // If it's been in Submitting or Unknown state for long, and it's not found, it means it never reached the exchange.
                        // However, we must NOT blindly submit another order.
                        // We will mark it as ManualInterventionRequired to avoid unsafe retry when duplicate protection cannot be guaranteed.
                        op.Status = OrderState.Unknown;
                        op.FailureCode = "NOT_FOUND_ON_RECOVERY_EXCHANGE";
                        op.UpdatedAt = DateTime.UtcNow;

                        await _orderRepository.UpdateAsync(op);

                        _monitoringService.IncrementMetric("ManualInterventions");
                        _monitoringService.EmitEvent("ManualInterventionRequired", $"Incomplete order {op.Id} not found on Exchange on recovery cycle. Manual Intervention is Required.",
                            new Dictionary<string, string> { { "OperationId", op.Id }, { "ExternalId", op.ExternalId } });
                    }
                }
                catch (Exception ex)
                {
                    // If we cannot contact exchange or DB, we cannot resolve state -> remains traceable
                    op.Status = OrderState.Unknown;
                    op.UpdatedAt = DateTime.UtcNow;
                    await _orderRepository.UpdateAsync(op);

                    _monitoringService.EmitEvent("RecoveryFailed", $"Failed to recover operation {op.Id}: {ex.Message}",
                        new Dictionary<string, string> { { "OperationId", op.Id }, { "Error", ex.Message } });
                }
            }

            return recoveredCount;
        }

        private OrderState ParseExchangeState(string state)
        {
            return state.ToUpperInvariant() switch
            {
                "SUBMITTED" or "NEW" or "PENDING" => OrderState.Submitted,
                "FILLED" or "PARTIALLYFILLED" => OrderState.Filled,
                "REJECTED" => OrderState.Rejected,
                "CANCELLED" => OrderState.Cancelled,
                "FAILED" => OrderState.Failed,
                _ => OrderState.Unknown
            };
        }
    }
}
