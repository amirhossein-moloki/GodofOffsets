#include "Core/OffsetDumper.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Core {

OffsetDumper::OffsetDumper(const ProcessManager& pm) : m_pm(pm) {}

std::vector<OffsetResult> OffsetDumper::DumpModule(const std::string& moduleName) {
    std::vector<OffsetResult> results;
    auto mod = m_pm.GetModuleInfo(moduleName);

    if (mod.baseAddress == 0) return results;

    results.push_back({ 0, moduleName, "Base", "uintptr_t", "" });

    for (const auto& section : mod.sections) {
        results.push_back({ section.virtualAddress - mod.baseAddress, moduleName, section.name, "Section", "" });
    }

    return results;
}

std::vector<OffsetResult> OffsetDumper::DumpStructure(uintptr_t baseAddress, const StructDefinition& def) {
    std::vector<OffsetResult> results;

    for (const auto& field : def.fields) {
        uintptr_t fieldAddr = baseAddress + field.offset;
        std::string value = FormatValue(fieldAddr, field.type);
        results.push_back({ field.offset, "", field.name, field.type, value });
    }

    return results;
}

std::string OffsetDumper::FormatValue(uintptr_t address, const std::string& type) {
    std::stringstream ss;
    if (type == "int32" || type == "Int32") {
        ss << m_pm.Read<int32_t>(address);
    } else if (type == "uint32" || type == "Uint32") {
        ss << m_pm.Read<uint32_t>(address);
    } else if (type == "float" || type == "Float") {
        ss << m_pm.Read<float>(address);
    } else if (type == "uintptr_t" || type == "Pointer") {
        ss << "0x" << std::hex << std::uppercase << m_pm.Read<uintptr_t>(address);
    } else {
        ss << "???";
    }
    return ss.str();
}

bool OffsetDumper::SaveToJSON(const std::string& filename, const std::vector<OffsetResult>& results) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& res : results) {
        j.push_back({
            {"offset", res.offset},
            {"module", res.moduleName},
            {"name", res.name},
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
    o << "Offset,Module,Name,Type,Value" << std::endl;
    for (const auto& res : results) {
        o << "0x" << std::hex << res.offset << "," << res.moduleName << "," << res.name << "," << res.type << "," << res.value << std::endl;
    }
    return true;
}

} // namespace Core
