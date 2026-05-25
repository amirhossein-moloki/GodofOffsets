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
    std::string type;
    std::string value;
};

class OffsetDumper {
public:
    OffsetDumper(const ProcessManager& pm);

    std::vector<OffsetResult> DumpModule(const std::string& moduleName);
    bool SaveToJSON(const std::string& filename, const std::vector<OffsetResult>& results);
    bool SaveToCSV(const std::string& filename, const std::vector<OffsetResult>& results);

private:
    const ProcessManager& m_pm;
};

} // namespace Core
