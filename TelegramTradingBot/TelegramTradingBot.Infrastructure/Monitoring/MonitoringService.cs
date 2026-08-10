using System.Collections.Concurrent;
using System.Collections.Generic;
using TelegramTradingBot.Core.Domain.Interfaces;

namespace TelegramTradingBot.Infrastructure.Monitoring
{
    public class MonitoringService : IMonitoringService
    {
        private readonly ConcurrentDictionary<string, int> _metrics = new();
        private readonly ConcurrentQueue<string> _events = new();

        public void EmitEvent(string eventName, string message, Dictionary<string, string>? metadata = null)
        {
            var metaStr = "";
            if (metadata != null && metadata.Count > 0)
            {
                var pairs = new List<string>();
                foreach (var kvp in metadata)
                {
                    // For security: Never log sensitive information
                    if (kvp.Key.ToLowerInvariant().Contains("secret") ||
                        kvp.Key.ToLowerInvariant().Contains("key") ||
                        kvp.Key.ToLowerInvariant().Contains("token") ||
                        kvp.Key.ToLowerInvariant().Contains("auth") ||
                        kvp.Key.ToLowerInvariant().Contains("signature"))
                    {
                        pairs.Add($"{kvp.Key}=[REDACTED]");
                    }
                    else
                    {
                        pairs.Add($"{kvp.Key}={kvp.Value}");
                    }
                }
                metaStr = " | " + string.Join(", ", pairs);
            }

            _events.Enqueue($"{eventName}: {message}{metaStr}");
        }

        public void IncrementMetric(string metricName)
        {
            _metrics.AddOrUpdate(metricName, 1, (_, val) => val + 1);
        }

        public int GetMetricValue(string metricName)
        {
            _metrics.TryGetValue(metricName, out var val);
            return val;
        }

        public IEnumerable<string> GetEmittedEvents()
        {
            return _events.ToArray();
        }

        public void Clear()
        {
            _metrics.Clear();
            _events.Clear();
        }
    }
}
