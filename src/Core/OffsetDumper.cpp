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

std::vector<OffsetResult> OffsetDumper::AnalyzeDataSections(const std::string& moduleName) {
    std::vector<OffsetResult> results;
    auto mod = m_pm.GetModuleInfo(moduleName);
    if (mod.baseAddress == 0) return results;

    auto allModules = m_pm.GetModules();

    for (const auto& section : mod.sections) {
        // Look for data sections (.data, .rdata, .bss, etc.)
        if (section.name.find(".data") != std::string::npos ||
            section.name.find(".rdata") != std::string::npos ||
            section.name.find(".bss") != std::string::npos) {

            std::vector<uint8_t> buffer(section.virtualSize);
            if (!m_pm.ReadMemory(section.virtualAddress, buffer.data(), section.virtualSize)) continue;

            for (size_t i = 0; i <= (section.virtualSize >= sizeof(uintptr_t) ? section.virtualSize - sizeof(uintptr_t) : 0); i += sizeof(uintptr_t)) {
                uintptr_t value = *(uintptr_t*)(buffer.data() + i);

                // Check if value is a pointer to any module
                for (const auto& targetMod : allModules) {
                    if (value >= targetMod.baseAddress && value < targetMod.baseAddress + targetMod.imageSize) {
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << value;
                        results.push_back({
                            section.virtualAddress + i - mod.baseAddress,
                            moduleName,
                            section.name + "+0x" + (static_cast<std::ostringstream&&>(std::ostringstream() << std::hex << i)).str(),
                            "Pointer",
                            targetMod.name + "+0x" + (static_cast<std::ostringstream&&>(std::ostringstream() << std::hex << (value - targetMod.baseAddress))).str()
                        });
                        break;
                    }
                }
            }
        }
    }

    return results;
}

std::vector<OffsetResult> OffsetDumper::DumpStructure(uintptr_t baseAddress, const StructDefinition& def, int count) {
    std::vector<OffsetResult> results;

    size_t structSize = 0;
    for (const auto& field : def.fields) {
        structSize = (std::max)(structSize, field.offset + 8); // Estimate size based on max offset
    }

    for (int i = 0; i < count; ++i) {
        uintptr_t currentBase = baseAddress + (i * structSize);
        for (const auto& field : def.fields) {
            uintptr_t fieldAddr = currentBase + field.offset;
            std::string value = FormatValue(fieldAddr, field.type);
            std::string name = field.name;
            if (count > 1) name += "[" + std::to_string(i) + "]";
            results.push_back({ (uintptr_t)(i * structSize + field.offset), "", name, field.type, value });
        }
    }

    return results;
}

std::vector<OffsetResult> OffsetDumper::DumpRange(uintptr_t baseAddress, size_t size, const std::string& type) {
    std::vector<OffsetResult> results;
    size_t typeSize = 4;
    if (type == "int64" || type == "uint64" || type == "uintptr_t" || type == "Pointer") typeSize = 8;
    else if (type == "int16" || type == "uint16") typeSize = 2;
    else if (type == "int8" || type == "uint8") typeSize = 1;

    for (size_t offset = 0; offset <= (size >= typeSize ? size - typeSize : 0); offset += typeSize) {
        uintptr_t addr = baseAddress + offset;
        std::string value = FormatValue(addr, type);
        results.push_back({ (uintptr_t)offset, "", "Offset_0x" + (static_cast<std::ostringstream&&>(std::ostringstream() << std::hex << offset)).str(), type, value });
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
        std::stringstream ss;
        if (!res.moduleName.empty()) {
            ss << res.moduleName << "+0x" << std::hex << std::uppercase << res.offset;
        } else {
            ss << "0x" << std::hex << std::uppercase << res.offset;
        }

        j.push_back({
            {"offset", ss.str()},
            {"module", res.moduleName},
            {"name", res.name},
            {"type", res.type},
            {"value", res.value},
            {"description", res.description}
        });
    }
    std::ofstream o(filename);
    if (!o.is_open()) return false;
    o << std::setw(4) << j << std::endl;
    return true;
}

bool OffsetDumper::LoadFromJSON(const std::string& filename, std::vector<OffsetResult>& results) {
    std::ifstream i(filename);
    if (!i.is_open()) return false;

    nlohmann::json j;
    try {
        i >> j;
        results.clear();
        for (const auto& item : j) {
            OffsetResult res;
            std::string offsetStr = item["offset"];
            size_t plusPos = offsetStr.find("+0x");
            if (plusPos != std::string::npos) {
                res.offset = std::stoull(offsetStr.substr(plusPos + 3), nullptr, 16);
            } else {
                res.offset = std::stoull(offsetStr, nullptr, 16);
            }

            res.moduleName = item.value("module", "");
            res.name = item.value("name", "");
            res.type = item.value("type", "");
            res.value = item.value("value", "");
            res.description = item.value("description", "");
            results.push_back(res);
        }
    } catch (...) {
        return false;
    }
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

bool OffsetDumper::SaveToText(const std::string& filename, const std::string& processName, DWORD pid, const std::vector<OffsetResult>& results) {
    std::ofstream o(filename);
    if (!o.is_open()) return false;

    o << "[Process]" << std::endl;
    o << "Name: " << processName << std::endl;
    o << "PID: " << pid << std::endl << std::endl;

    o << "[Modules]" << std::endl;
    std::vector<std::string> uniqueModules;
    for (const auto& res : results) {
        if (!res.moduleName.empty()) {
            bool found = false;
            for (const auto& m : uniqueModules) if (m == res.moduleName) { found = true; break; }
            if (!found) uniqueModules.push_back(res.moduleName);
        }
    }
    // Also include the process name itself if not present
    bool procFound = false;
    for (const auto& m : uniqueModules) if (m == processName) { procFound = true; break; }
    if (!procFound) uniqueModules.push_back(processName);

    for (const auto& modName : uniqueModules) {
        uintptr_t base = m_pm.GetModuleBase(modName);
        if (base) {
            o << modName << ": 0x" << std::hex << std::uppercase << base << std::endl;
        }
    }
    o << std::endl;

    o << "[Offsets]" << std::endl;
    for (const auto& res : results) {
        if (res.type == "Section" || res.name == "Base") continue;
        o << res.name << ": " << res.moduleName << " + 0x" << std::hex << std::uppercase << res.offset << " = 0x" << res.value << std::endl;
    }

    return true;
}

} // namespace Core
