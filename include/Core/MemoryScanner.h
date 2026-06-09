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

/**
 * @brief Search comparison types for scanning.
 * @details انواع جستجو برای اسکنر حافظه
 */
enum class ScanType {
    ExactValue,     /**< Search for a specific value / جستجوی مقدار دقیق */
    UnknownInitial, /**< Initial scan for unknown values / اسکن اولیه برای مقادیر نامعلوم */
    Increased,      /**< Search for values that have increased / مقادیر افزایش یافته */
    Decreased,      /**< Search for values that have decreased / مقادیر کاهش یافته */
    Changed,        /**< Search for values that have changed / مقادیر تغییر کرده */
    Unchanged,      /**< Search for values that haven't changed / مقادیر بدون تغییر */
    GreaterThan,    /**< Search for values > target / مقادیر بزرگتر از هدف */
    LessThan,       /**< Search for values < target / مقادیر کوچکتر از هدف */
    Between         /**< Search for values in range [v1, v2] / مقادیر در محدوده مشخص شده */
};

/**
 * @brief Supported data types for scanning.
 * @details انواع داده پشتیبانی شده برای اسکن
 */
enum class DataType {
    Int8, Uint8,
    Int16, Uint16,
    Int32, Uint32,
    Int64, Uint64,
    Float, Double,
    String, String16,
    AOB             /**< Array of Bytes (Hex pattern) / آرایه‌ای از بایت‌ها (پترن هگز) */
};

/**
 * @brief Represents a value to scan for.
 * @details نمایانگر یک مقدار برای جستجو در حافظه
 */
struct ScanValue {
    DataType type;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string, std::vector<uint8_t>> value;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double> value2;
};

/**
 * @brief High-performance asynchronous memory scanner.
 * @details اسکنر حافظه با کارایی بالا و قابلیت پردازش ناهمگام
 */
class MemoryScanner {
public:
    MemoryScanner(const ProcessManager& pm);

    /**
     * @brief Initiates a first-pass scan of the process memory.
     * @param val Target value and type.
     * @param scanType Comparison method.
     * @param modifyProtection Temporarily change memory protection.
     * @details شروع اولین مرحله اسکن در کل فضای حافظه
     */
    void FirstScan(const ScanValue& val, ScanType scanType, bool modifyProtection = false);

    /**
     * @brief Filters existing results in a second-pass scan.
     * @details فیلتر کردن نتایج مرحله قبل (Next Scan)
     */
    void NextScan(const ScanValue& val, ScanType scanType, bool modifyProtection = false);

    /**
     * @brief Reverts the last scan action.
     * @details بازگشت به مرحله قبلی اسکن
     */
    void Undo();

    /**
     * @brief Retrieves the list of found addresses.
     * @details دریافت لیست آدرس‌های پیدا شده
     */
    std::vector<uintptr_t> GetResults();

    /**
     * @brief Retrieves values corresponding to the results from the time of scan.
     * @details دریافت مقادیر مربوط به آدرس‌ها در زمان اسکن
     */
    std::vector<uint8_t> GetResultValues();

    /**
     * @brief Gets total number of results found.
     * @details دریافت تعداد کل نتایج
     */
    size_t GetResultCount();

    /**
     * @brief Resets the scanner state.
     * @details بازنشانی کامل اسکنر
     */
    void Reset();

    /**
     * @brief Checks if a scan is currently active.
     * @details بررسی در حال اجرا بودن اسکن
     */
    bool IsScanning() const { return m_isScanning; }

    /**
     * @brief Gets current scan progress (0.0 to 1.0).
     * @details دریافت درصد پیشرفت اسکن (۰ تا ۱)
     */
    float GetProgress() const { return m_progress; }

    /**
     * @brief Requests cancellation of the active scan.
     * @details درخواست لغو اسکن در حال اجرا
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
