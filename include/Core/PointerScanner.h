#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <atomic>
#include <unordered_map>
#include <set>
#include <mutex>
#include "Core/ProcessManager.h"

namespace Core {

/**
 * @brief Represents a discovered pointer chain.
 * @details نمایانگر یک زنجیره اشاره‌گر کشف شده
 */
struct PointerChain {
    uintptr_t baseAddress;      /**< Base address of the starting module / آدرس پایه ماژول شروع‌کننده */
    std::string moduleName;     /**< Name of the starting module / نام ماژول شروع‌کننده */
    std::vector<uintptr_t> offsets; /**< Sequence of offsets to reach the target / توالی آفست‌ها برای رسیدن به مقصد */
};

/**
 * @brief High-performance pointer chain discovery engine.
 * @details موتور کشف زنجیره اشاره‌گر با کارایی بالا
 */
class PointerScanner {
public:
    PointerScanner(const ProcessManager& pm);

    /**
     * @brief Starts an asynchronous scan for pointer chains.
     * @param targetAddress The final address to find chains for.
     * @param maxDepth Maximum number of pointers in a chain.
     * @param maxOffset Maximum allowed offset between pointers.
     * @details شروع اسکن ناهمگام برای یافتن زنجیره‌های اشاره‌گر
     */
    void StartScan(uintptr_t targetAddress, int maxDepth, size_t maxOffset);

    /**
     * @brief Retrieves all discovered pointer chains.
     * @details دریافت تمامی زنجیره‌های پیدا شده
     */
    std::vector<PointerChain> GetResults();

    /**
     * @brief Checks if a scan is currently active.
     * @details بررسی در حال اجرا بودن اسکن
     */
    bool IsScanning() const { return m_isScanning; }

    /**
     * @brief Gets current scan progress (0.0 to 1.0).
     * @details دریافت درصد پیشرفت اسکن
     */
    float GetProgress() const { return m_progress; }

    /**
     * @brief Requests cancellation of the active scan.
     * @details درخواست لغو اسکن فعال
     */
    void Cancel() { m_cancelRequested = true; }

private:
    const ProcessManager& m_pm;
    std::vector<PointerChain> m_results;
    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<float> m_progress{0.0f};
    std::mutex m_resultsMutex;

    // Two-stage pointer scanning
    std::unordered_multimap<uintptr_t, uintptr_t> m_pointerMap;
    void BuildPointerMap();
    void FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::set<uintptr_t>& visited);
};

} // namespace Core
