#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <variant>
#include <atomic>
#include <functional>
#include <stack>
#include <mutex>
#include <future>
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
    String, String16,
    AOB
};

struct ScanValue {
    DataType type;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string, std::vector<uint8_t>> value;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double> value2;
};

/**
 * @class MemoryScanner
 * @brief Handles multi-threaded memory scanning for values and AOBs.
 * @details کلاس اسکن حافظه برای مقادیر عددی، رشته‌ها و آرایه‌ای از بایت‌ها با قابلیت چند-رشته‌ای.
 */
class MemoryScanner {
public:
    MemoryScanner(const ProcessManager& pm);

    /**
     * @brief Initiates a first-pass scan across all committed memory regions.
     * @param val The value to search for.
     * @param scanType Comparison method (Exact, Greater, etc.).
     * @param modifyProtection If true, reads through restricted memory protections.
     */
    void FirstScan(const ScanValue& val, ScanType scanType, bool modifyProtection = false);

    /**
     * @brief Filters the current results using a new comparison.
     * @details فیلتر کردن نتایج قبلی با شرایط جدید.
     */
    void NextScan(const ScanValue& val, ScanType scanType, bool modifyProtection = false);

    /**
     * @brief Reverts the last scan filtering operation.
     * @details بازگشت به نتایج مرحله قبل (Undo).
     */
    void Undo();

    /**
     * @brief Retrieves the list of addresses found in the last scan.
     * @return std::vector<uintptr_t> لیست آدرس‌های پیدا شده.
     */
    std::vector<uintptr_t> GetResults();

    /**
     * @brief Retrieves the raw byte values associated with the found addresses.
     * @return std::vector<uint8_t> مقادیر بایت خام نتایج.
     */
    std::vector<uint8_t> GetResultValues();

    /**
     * @brief Returns the total number of results found.
     * @return size_t تعداد نتایج.
     */
    size_t GetResultCount();

    /**
     * @brief Resets the current scan results and history.
     * @details پاکسازی نتایج جستجوی فعلی و تاریخچه.
     */
    void Reset();

    /**
     * @brief Checks if a background scan is currently in progress.
     * @return true if scanning.
     */
    bool IsScanning() const { return m_isScanning; }

    /**
     * @brief Returns the progress of the current scan (0.0 to 1.0).
     */
    float GetProgress() const { return m_progress; }

    /**
     * @brief Requests cancellation of the active background scan.
     * @details درخواست لغو عملیات اسکن پس‌زمینه.
     */
    void Cancel();

private:
    struct ScanSnapshot {
        std::vector<uintptr_t> addresses;
        std::vector<uint8_t> values;
    };

    const ProcessManager& m_pm;
    ScanSnapshot m_currentScan;
    std::stack<ScanSnapshot> m_history;
    std::mutex m_resultsMutex;

    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<float> m_progress{0.0f};
    std::future<void> m_scanFuture;

    void ScanRegion(const RegionInfo& region, const ScanValue& val, ScanType scanType, std::vector<uintptr_t>& localResults, std::vector<uint8_t>& localValues, bool modifyProtection = false);
    bool CompareValues(const void* current, const void* previous, const ScanValue& val, ScanType scanType, size_t size);

    void AOBScan(const RegionInfo& region, const std::string& pattern, std::vector<uintptr_t>& results, std::vector<uint8_t>& values);
};

} // namespace Core
