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
};

class OffsetDumper {
public:
    OffsetDumper(const ProcessManager& pm);

    std::vector<OffsetResult> DumpModule(const std::string& moduleName);
    std::vector<OffsetResult> DumpStructure(uintptr_t baseAddress, const StructDefinition& def, int count = 1);
    std::vector<OffsetResult> DumpRange(uintptr_t baseAddress, size_t size, const std::string& type);
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
