#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "Core/ProcessManager.h"

namespace Core {

struct OffsetResult {
    uintptr_t offset;
    std::string moduleName;
    std::string name;
    std::string type;
    std::string value;
    std::string description;
};

struct StructField {
    std::string name;
    std::string type;
    size_t offset;
};

struct StructDefinition {
    std::string name;
    std::vector<StructField> fields;
    size_t structSize = 0;
};

/**
 * @class OffsetDumper
 * @brief Provides functionality to export memory structures and module offsets.
 * @details کلاسی برای استخراج (دامپ) آفست‌ها، ساختارها و تحلیل بخش‌های داده‌ای ماژول‌ها.
 */
class OffsetDumper {
public:
    OffsetDumper(const ProcessManager& pm);

    /**
     * @brief Dumps all section offsets for a given module.
     * @details استخراج آفست‌های بخش‌های (Sections) یک ماژول خاص.
     */
    std::vector<OffsetResult> DumpModule(const std::string& moduleName);

    /**
     * @brief Dumps a range of memory as an array of a specific data type.
     * @details استخراج محدوده‌ای از حافظه به صورت آرایه‌ای از یک نوع داده خاص.
     */
    std::vector<OffsetResult> DumpRange(uintptr_t start, uintptr_t end, const std::string& type);

    /**
     * @brief Dumps multiple instances of a user-defined structure.
     * @details استخراج چندین نمونه از یک ساختار (Struct) تعریف شده توسط کاربر.
     */
    std::vector<OffsetResult> DumpStructure(uintptr_t baseAddress, const StructDefinition& def, int count = 1);

    /**
     * @brief Analyzes data sections of a module to find potential pointers.
     * @details تحلیل بخش‌های داده‌ای (.data, .rdata) برای یافتن اشاره‌گرهای احتمالی.
     */
    std::vector<OffsetResult> AnalyzeDataSections(const std::string& moduleName);

    /**
     * @brief Saves the results to a structured JSON file.
     * @details ذخیره نتایج در یک فایل با فرمت JSON.
     */
    bool SaveToJSON(const std::string& filename, const std::vector<OffsetResult>& results);
    bool LoadFromJSON(const std::string& filename, std::vector<OffsetResult>& results);
    bool SaveToCSV(const std::string& filename, const std::vector<OffsetResult>& results);
    bool SaveToText(const std::string& filename, const std::string& processName, DWORD pid, const std::vector<OffsetResult>& results);

private:
    const ProcessManager& m_pm;

    std::string FormatValue(uintptr_t address, const std::string& type);
};

} // namespace Core
