#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "Core/ProcessManager.h"
#include "Core/OffsetResolver.h"

namespace Core {

/**
 * @brief Represents a signature to search for.
 * @details نمایانگر یک امضا (Signatue) برای جستجو
 */
struct Signature {
    std::string name;
    std::string pattern;        /**< Hex pattern with wildcards / پترن هگز به همراه وایلدکارد */
    std::string moduleName;     /**< Target module to scan / ماژول هدف برای اسکن */
    int offset = 0;             /**< Offset to add to result / آفست برای اضافه کردن به نتیجه نهایی */
    bool isRelative = true;     /**< If true, resolves RIP-relative addresses / حل کردن آدرس‌های نسبی RIP */
    uintptr_t result = 0;       /**< Discovered address / آدرس کشف شده */
};

/**
 * @brief Signature and AOB scanner with RIP-relative resolution.
 * @details اسکنر امضا و AOB با قابلیت حل آدرس‌های نسبی
 */
class Scanner {
public:
    Scanner(const ProcessManager& pm);

    /**
     * @brief Finds a single pattern in a module's memory.
     * @return The absolute address of the match.
     * @details یافتن یک پترن خاص در حافظه یک ماژول
     */
    uintptr_t FindPattern(const std::string& moduleName, const std::string& pattern);

    /**
     * @brief Loads signatures from a JSON file.
     * @details بارگذاری لیست امضاها از یک فایل JSON
     */
    std::vector<Signature> LoadSignatures(const std::string& filename);

    /**
     * @brief Runs a batch of signature scans.
     * @details اجرای دسته‌ای اسکن‌های امضا
     */
    void Run(std::vector<Signature>& sigs, bool isVulkan = false);

protected:
    const ProcessManager& m_pm;
    OffsetResolver m_resolver;

    uintptr_t ScanInternal(uintptr_t base, size_t size, const std::string& pattern);

private:
    std::vector<uint8_t> ParsePattern(const std::string& pattern, std::vector<bool>& mask);
};

} // namespace Core
