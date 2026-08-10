using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TelegramTradingBot.Core.Domain.Entities;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Core.Application.Services
{
    public class SignalProcessor
    {
        private readonly ISignalRepository _signalRepository;
        private readonly IMonitoringService _monitoringService;

        public SignalProcessor(ISignalRepository signalRepository, IMonitoringService monitoringService)
        {
            _signalRepository = signalRepository;
            _monitoringService = monitoringService;
        }

        /// <summary>
        /// Processes an incoming signal safely and idempotently.
        /// Returns true if the signal is new and successfully processed, false if duplicate.
        /// </summary>
        public async Task<bool> ProcessSignalAsync(Signal signal)
        {
            // Calculate / Read Signal Identity deterministically
            if (string.IsNullOrWhiteSpace(signal.Id))
            {
                // Source + ChatId + MessageId
                signal.Id = $"{signal.Source}:{signal.ChatId}:{signal.MessageId}";
            }

            // Step 1: App-level duplicate detection
            if (await _signalRepository.ExistsAsync(signal.Id))
            {
                _monitoringService.IncrementMetric("DuplicateSignals");
                _monitoringService.EmitEvent("DuplicateSignalDetected", $"Signal already processed: {signal.Id}",
                    new Dictionary<string, string> { { "SignalId", signal.Id } });
                return false;
            }

            // Step 2: Database enforcement. Attempt to save the signal.
            // If another worker saved it concurrently, unique constraint violation will occur.
            try
            {
                await _signalRepository.AddAsync(signal);
            }
            catch (Exception ex) when (ex.Message.Contains("UniqueConstraint") || ex.Message.Contains("DuplicateKey") || ex.GetType().Name.Contains("Unique") || ex.GetType().Name.Contains("Duplicate"))
            {
                // Race Condition Handled safely as a duplicate
                _monitoringService.IncrementMetric("DuplicateSignals");
                _monitoringService.EmitEvent("DuplicateSignalDetected", $"Concurrent worker saved signal: {signal.Id}",
                    new Dictionary<string, string> { { "SignalId", signal.Id }, { "Reason", "UniqueConstraintViolation" } });
                return false;
            }

            // Success: Process signal
            _monitoringService.EmitEvent("SignalProcessed", $"Successfully processed signal: {signal.Id}",
                new Dictionary<string, string> { { "SignalId", signal.Id } });
            return true;
        }
    }
}
