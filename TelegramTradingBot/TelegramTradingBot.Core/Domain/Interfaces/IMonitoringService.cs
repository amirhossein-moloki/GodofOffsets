using System.Collections.Generic;

namespace TelegramTradingBot.Core.Domain.Interfaces
{
    public interface IMonitoringService
    {
        void EmitEvent(string eventName, string message, Dictionary<string, string>? metadata = null);
        void IncrementMetric(string metricName);
        int GetMetricValue(string metricName);
        IEnumerable<string> GetEmittedEvents();
    }
}
