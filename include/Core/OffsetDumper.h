#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "Core/ProcessManager.h"

namespace Core {

/**
 * @brief Represents a single offset result.
 * @details نمایانگر نتیجه یک آفست استخراج شده
 */
struct OffsetResult {
    uintptr_t offset;           /**< Offset relative to module base / آفست نسبت به پایه ماژول */
    std::string moduleName;
    std::string name;
    std::string type;
    std::string value;
    std::string description;
};

/**
 * @brief Definition of a single field in a structure.
 * @details تعریف یک فیلد خاص در ساختار داده
 */
struct StructField {
    std::string name;
    std::string type;
    size_t offset;
};

/**
 * @brief Collection of fields defining a structure.
 * @details مجموعه‌ای از فیلدها که یک ساختار را تعریف می‌کنند
 */
struct StructDefinition {
    std::string name;
    std::vector<StructField> fields;
    size_t structSize = 0;
};

/**
 * @brief Handles dumping of memory ranges and structures.
 * @details مدیریت استخراج محدوده‌های حافظه و ساختارهای داده
 */
class OffsetDumper {
public:
    OffsetDumper(const ProcessManager& pm);

    /**
     * @brief Dumps all section information for a module.
     * @details استخراج تمامی اطلاعات سکشن‌های یک ماژول
     */
    std::vector<OffsetResult> DumpModule(const std::string& moduleName);

    /**
     * @brief Dumps a raw memory range as an array of values.
     * @details استخراج خام یک محدوده حافظه به صورت آرایه‌ای از مقادیر
     */
    std::vector<OffsetResult> DumpRange(uintptr_t start, uintptr_t end, const std::string& type);

    /**
     * @brief Dumps multiple instances of a structure from a base address.
     * @details استخراج چندین نمونه از یک ساختار از یک آدرس پایه
     */
    std::vector<OffsetResult> DumpStructure(uintptr_t baseAddress, const StructDefinition& def, int count = 1);

    /**
     * @brief Automatically identifies potential pointers in data sections.
     * @details شناسایی خودکار اشاره‌گرهای احتمالی در سکشن‌های داده
     */
    std::vector<OffsetResult> AnalyzeDataSections(const std::string& moduleName);

    /**
     * @brief Discovers potential pointer fields within a memory range.
     * @param baseAddress The starting address of the structure or range.
     * @param rangeSize The size of the range to scan.
     * @return A vector of discovered fields.
     * @details شناسایی خودکار فیلدهای اشاره‌گر در یک محدوده حافظه
     */
    std::vector<StructField> AutoDiscoverFields(uintptr_t baseAddress, size_t rangeSize) const;

    /**
     * @brief Exports results to a JSON file.
     * @details خروجی گرفتن از نتایج در قالب فایل JSON
     */
    bool SaveToJSON(const std::string& filename, const std::vector<OffsetResult>& results);

    /**
     * @brief Loads results from a JSON file.
     * @details بارگذاری نتایج از یک فایل JSON
     */
    bool LoadFromJSON(const std::string& filename, std::vector<OffsetResult>& results);

    /**
     * @brief Exports results to a CSV file.
     * @details خروجی گرفتن از نتایج در قالب فایل CSV
     */
    bool SaveToCSV(const std::string& filename, const std::vector<OffsetResult>& results);

    /**
     * @brief Exports results to a human-readable text file.
     * @details خروجی گرفتن از نتایج در قالب فایل متنی خوانا
     */
    bool SaveToText(const std::string& filename, const std::string& processName, DWORD pid, const std::vector<OffsetResult>& results);

private:
    const ProcessManager& m_pm;

    std::string FormatValue(uintptr_t address, const std::string& type);
};

} // namespace Core
