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
 * @brief Extracts and analyzes offsets and structures from target processes.
 * استخراج و تحلیل آفست‌ها و ساختارها از پردازش‌های هدف.
 */
class OffsetDumper {
public:
    OffsetDumper(const ProcessManager& pm);

    /**
     * @brief Dumps section and base info for a module.
     * دامپ کردن اطلاعات بخش‌ها و بیس یک ماژول.
     */
    std::vector<OffsetResult> DumpModule(const std::string& moduleName);
    std::vector<OffsetResult> DumpRange(uintptr_t start, uintptr_t end, const std::string& type);
    /**
     * @brief Dumps a structure from a base address based on a definition.
     * دامپ کردن یک ساختار از آدرس بیس بر اساس تعریف داده شده.
     */
    std::vector<OffsetResult> DumpStructure(uintptr_t baseAddress, const StructDefinition& def, int count = 1);

    /**
     * @brief Analyzes data sections of a module to find potential pointers.
     * تحلیل بخش‌های داده‌ای یک ماژول برای پیدا کردن اشاره‌گرهای احتمالی.
     */
    std::vector<OffsetResult> AnalyzeDataSections(const std::string& moduleName);

    bool SaveToJSON(const std::string& filename, const std::vector<OffsetResult>& results);
    bool LoadFromJSON(const std::string& filename, std::vector<OffsetResult>& results);
    bool SaveToCSV(const std::string& filename, const std::vector<OffsetResult>& results);
    bool SaveToText(const std::string& filename, const std::string& processName, DWORD pid, const std::vector<OffsetResult>& results);

private:
    const ProcessManager& m_pm;

    std::string FormatValue(uintptr_t address, const std::string& type);
};

} // namespace Core
