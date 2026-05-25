#include "Core/OffsetDumper.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Core {

OffsetDumper::OffsetDumper(const ProcessManager& pm) : m_pm(pm) {}

std::vector<OffsetResult> OffsetDumper::DumpModule(const std::string& moduleName) {
    std::vector<OffsetResult> results;
    auto modules = m_pm.GetModules();

    const ModuleInfo* targetMod = nullptr;
    for (const auto& mod : modules) {
        if (mod.name == moduleName) {
            targetMod = &mod;
            break;
        }
    }

    if (!targetMod) return results;

    // Simplified dump: just some sample addresses or PE sections
    // In a real dumper, we would parse PE headers and find exports or specific signatures

    results.push_back({ 0, moduleName, "BaseAddress", "" });

    return results;
}

bool OffsetDumper::SaveToJSON(const std::string& filename, const std::vector<OffsetResult>& results) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& res : results) {
        j.push_back({
            {"offset", res.offset},
            {"module", res.moduleName},
            {"type", res.type},
            {"value", res.value}
        });
    }
    std::ofstream o(filename);
    if (!o.is_open()) return false;
    o << std::setw(4) << j << std::endl;
    return true;
}

bool OffsetDumper::SaveToCSV(const std::string& filename, const std::vector<OffsetResult>& results) {
    std::ofstream o(filename);
    if (!o.is_open()) return false;
    o << "Offset,Module,Type,Value" << std::endl;
    for (const auto& res : results) {
        o << "0x" << std::hex << res.offset << "," << res.moduleName << "," << res.type << "," << res.value << std::endl;
    }
    return true;
}

} // namespace Core
