using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Enums;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Core.Application.Services
{
    public class OrderSubmissionService
    {
        private readonly IOrderRepository _orderRepository;
        private readonly IExchangeClient _exchangeClient;
        private readonly IMonitoringService _monitoringService;

        public OrderSubmissionService(
            IOrderRepository orderRepository,
            IExchangeClient exchangeClient,
            IMonitoringService monitoringService)
        {
            _orderRepository = orderRepository;
            _exchangeClient = exchangeClient;
            _monitoringService = monitoringService;
        }

        public async Task<OrderOperation> SubmitOrderIdempotentlyAsync(
            string signalId,
            string symbol,
            string side,
            decimal qty,
            decimal? price,
            string correlationId)
        {
            // Generate / Resolve Idempotency Key
            // Key is unique and deterministic for the logical operation
            string idempotencyKey = $"{signalId}:OrderSubmission:{side}";

            OrderOperation operation;

            // Check if we already have an operation record for this key (multi-instance/concurrent protection)
            OrderOperation? existingOp = await _orderRepository.GetByIdempotencyKeyAsync(idempotencyKey);
            if (existingOp != null)
            {
                // If transitional (Submitting / Unknown / Submitted), we resolve it first
                if (existingOp.Status == OrderState.Submitting ||
                    existingOp.Status == OrderState.Unknown ||
                    existingOp.Status == OrderState.Submitted)
                {
                    existingOp = await ResolveUnknownOrderStateAsync(existingOp);
                }

                // If after resolution, the order is transitional or successful terminal state, we do not re-submit
                if (existingOp.Status != OrderState.NotSubmitted && existingOp.Status != OrderState.Failed)
                {
                    _monitoringService.IncrementMetric("DuplicateOrdersPrevented");
                    _monitoringService.EmitEvent("DuplicateOrderPrevented", $"Duplicate submission prevented for key: {idempotencyKey}",
                        new Dictionary<string, string> { { "IdempotencyKey", idempotencyKey }, { "Status", existingOp.Status.ToString() } });
                    return existingOp;
                }

                // If it is NotSubmitted or Failed, we can safely retry it!
                existingOp.Status = OrderState.Submitting;
                existingOp.UpdatedAt = DateTime.UtcNow;
                await _orderRepository.UpdateAsync(existingOp);
                operation = existingOp;
            }
            else
            {
                // Create and persist new Operation Record in Submitting state
                operation = new OrderOperation
                {
                    Id = Guid.NewGuid().ToString(),
                    IdempotencyKey = idempotencyKey,
                    OperationType = "OrderSubmission",
                    CorrelationId = correlationId,
                    Status = OrderState.Submitting,
                    ExternalId = idempotencyKey, // Bybit client order id uses the idempotency key or derived ID
                    Symbol = symbol,
                    Side = side,
                    Qty = qty,
                    Price = price,
                    CreatedAt = DateTime.UtcNow,
                    UpdatedAt = DateTime.UtcNow
                };

                try
                {
                    await _orderRepository.AddAsync(operation);
                }
                catch (Exception ex) when (ex.Message.Contains("UniqueConstraint") || ex.Message.Contains("DuplicateKey") || ex.GetType().Name.Contains("Unique") || ex.GetType().Name.Contains("Duplicate"))
                {
                    // DB unique constraint protected us from concurrent duplicate operations!
                    _monitoringService.IncrementMetric("DuplicateOrdersPrevented");
                    _monitoringService.EmitEvent("DuplicateOrderPrevented", $"Concurrency DB collision prevented duplicate key: {idempotencyKey}");

                    var op = await _orderRepository.GetByIdempotencyKeyAsync(idempotencyKey);
                    if (op != null && (op.Status == OrderState.Submitting || op.Status == OrderState.Unknown || op.Status == OrderState.Submitted))
                    {
                        op = await ResolveUnknownOrderStateAsync(op);
                    }
                    return op ?? throw new InvalidOperationException("Failed to load existing operation after DB collision.");
                }
            }

            // Check Existing Exchange State before making the submission (to be extremely safe)
            var (found, exchangeState, failureCode) = await _exchangeClient.QueryOrderAsync(operation.ExternalId);
            if (found)
            {
                _monitoringService.EmitEvent("OrderStateRecovered", $"Order already exists on exchange before submission: {operation.ExternalId}",
                    new Dictionary<string, string> { { "IdempotencyKey", idempotencyKey }, { "ExchangeState", exchangeState } });

                operation.Status = ParseExchangeState(exchangeState);
                operation.FailureCode = failureCode;
                operation.UpdatedAt = DateTime.UtcNow;
                if (operation.Status == OrderState.Filled || operation.Status == OrderState.Rejected || operation.Status == OrderState.Cancelled || operation.Status == OrderState.Failed)
                {
                    operation.CompletedAt = DateTime.UtcNow;
                }
                await _orderRepository.UpdateAsync(operation);
                return operation;
            }

            // Submit Order to the exchange
            try
            {
                string exchangeOrderId = await _exchangeClient.SubmitOrderAsync(operation);

                // Submit succeeded
                operation.Status = OrderState.Submitted;
                operation.UpdatedAt = DateTime.UtcNow;

                try
                {
                    await _orderRepository.UpdateAsync(operation);
                }
                catch (Exception dbEx)
                {
                    // PARTIAL FAILURE (Section 18): Exchange Order Created, but Local Database Update Failed
                    _monitoringService.EmitEvent("DatabaseFailureAfterExchangeCreation", $"Failed updating local DB after order created on exchange: {dbEx.Message}",
                        new Dictionary<string, string> { { "ExternalId", operation.ExternalId }, { "Error", dbEx.Message } });
                }
            }
            catch (TimeoutException timeoutEx)
            {
                // UNKNOWN ORDER STATE (Section 13)
                operation.Status = OrderState.Unknown;
                operation.FailureCode = "TIMEOUT";
                operation.UpdatedAt = DateTime.UtcNow;

                _monitoringService.IncrementMetric("UnknownOrders");
                _monitoringService.EmitEvent("OrderStateUnknown", $"Timeout submitting order: {timeoutEx.Message}",
                    new Dictionary<string, string> { { "IdempotencyKey", idempotencyKey } });

                try
                {
                    await _orderRepository.UpdateAsync(operation);
                }
                catch
                {
                    // Ignore DB error
                }

                // Query Exchange immediately to attempt resolution
                return await ResolveUnknownOrderStateAsync(operation);
            }
            catch (Exception ex)
            {
                operation.Status = OrderState.Failed;
                operation.FailureCode = ex.Message;
                operation.UpdatedAt = DateTime.UtcNow;
                operation.CompletedAt = DateTime.UtcNow;

                try
                {
                    await _orderRepository.UpdateAsync(operation);
                }
                catch
                {
                    // Ignore DB errors
                }
                throw;
            }

            return operation;
        }

        public async Task<OrderOperation> ResolveUnknownOrderStateAsync(OrderOperation operation)
        {
            _monitoringService.EmitEvent("OperationRecoveryStarted", $"Resolving unknown state for: {operation.IdempotencyKey}");

            var (found, exchangeState, failureCode) = await _exchangeClient.QueryOrderAsync(operation.ExternalId);
            if (found)
            {
                operation.Status = ParseExchangeState(exchangeState);
                operation.FailureCode = failureCode;
                operation.UpdatedAt = DateTime.UtcNow;
                if (operation.Status == OrderState.Filled || operation.Status == OrderState.Rejected || operation.Status == OrderState.Cancelled || operation.Status == OrderState.Failed)
                {
                    operation.CompletedAt = DateTime.UtcNow;
                }

                await _orderRepository.UpdateAsync(operation);

                _monitoringService.IncrementMetric("RecoveredOperations");
                _monitoringService.EmitEvent("OrderStateRecovered", $"Recovered operation state from exchange: {operation.Status}",
                    new Dictionary<string, string> { { "IdempotencyKey", operation.IdempotencyKey }, { "NewState", operation.Status.ToString() } });
            }
            else
            {
                // Order not found on Exchange -> Safe to evaluate retry (it never reached the exchange)
                _monitoringService.EmitEvent("OrderNotFoundOnExchange", $"Order {operation.ExternalId} not found on exchange. Marking as NotSubmitted for safe retry.");

                operation.Status = OrderState.NotSubmitted;
                operation.FailureCode = "NOT_FOUND_ON_EXCHANGE";
                operation.UpdatedAt = DateTime.UtcNow;
                await _orderRepository.UpdateAsync(operation);
            }

            return operation;
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
