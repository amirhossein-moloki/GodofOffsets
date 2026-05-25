#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <variant>
#include <atomic>
#include <functional>
#include "Core/ProcessManager.h"

namespace Core {

enum class ScanType {
    ExactValue,
    UnknownInitial,
    Increased,
    Decreased,
    Changed,
    Unchanged,
    GreaterThan,
    LessThan,
    Between
};

enum class DataType {
    Int8, Uint8,
    Int16, Uint16,
    Int32, Uint32,
    Int64, Uint64,
    Float, Double,
    String,
    AOB
};

struct ScanValue {
    DataType type;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string, std::vector<uint8_t>> value;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double> value2; // For "Between" scan
};

class MemoryScanner {
public:
    MemoryScanner(const ProcessManager& pm);

    void FirstScan(const ScanValue& val, ScanType scanType);
    void NextScan(const ScanValue& val, ScanType scanType);

    const std::vector<uintptr_t>& GetResults() const { return m_results; }
    void Reset();

    bool IsScanning() const { return m_isScanning; }
    float GetProgress() const { return m_progress; }
    void Cancel();

private:
    const ProcessManager& m_pm;
    std::vector<uintptr_t> m_results;
    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<float> m_progress{0.0f};

    void ScanRegion(const RegionInfo& region, const ScanValue& val, ScanType scanType, std::vector<uintptr_t>& localResults);
    bool CompareValues(const void* mem, const ScanValue& val, ScanType scanType, size_t size);

    // SIMD AOB Scan
    std::vector<uintptr_t> AOBScan(const RegionInfo& region, const std::string& pattern);
};

} // namespace Core
