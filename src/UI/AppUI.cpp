#include "UI/AppUI.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace UI {

AppUI::AppUI(Core::ProcessManager& pm, Core::Scanner& scanner)
    : m_pm(pm), m_scanner(scanner), m_memScanner(pm), m_ptrScanner(pm), m_dumper(pm) {
    m_sigs = m_scanner.LoadSignatures("signatures.json");
}

void AppUI::Render() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Universal Offset Dumper", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    RenderHeader();
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Process")) {
            RenderProcessTab();
            ImGui::EndTabItem();
        }
        if (m_isAttached) {
            if (ImGui::BeginTabItem("Memory Scanner")) {
                RenderMemoryScannerTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Signature Scanner")) {
                RenderSignatureTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Pointer Scan")) {
                RenderPointerScanTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Structure Dumper")) {
                RenderDumperTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Hex Viewer")) {
                RenderHexViewerTab();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void AppUI::RenderHeader() {
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Universal Offset Dumper v1.0");
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::Text("Status: %s", m_status.c_str());
    if (m_memScanner.IsScanning() || m_ptrScanner.IsScanning()) {
        ImGui::SameLine();
        ImGui::ProgressBar(m_memScanner.GetProgress(), ImVec2(100, 0));
    }
}

void AppUI::RenderProcessTab() {
    ImGui::InputText("Process Name", m_processName, sizeof(m_processName));

    if (ImGui::Button("Attach (Standard)")) {
        if (m_pm.Attach(m_processName, Core::MemoryMode::Standard)) {
            m_isAttached = true;
            m_status = "Attached (Standard)";
            m_memScanner.Reset();
        } else {
            m_status = "Failed to Attach";
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Attach (Stealth)")) {
        if (m_pm.Attach(m_processName, Core::MemoryMode::Stealth)) {
            m_isAttached = true;
            m_status = "Attached (Stealth Mode)";
            m_memScanner.Reset();
        } else {
            m_status = "Failed to Attach Stealth";
        }
    }

    if (m_isAttached) {
        ImGui::Separator();
        ImGui::Text("PID: %d | Mode: %s", m_pm.GetPid(), m_pm.GetMode() == Core::MemoryMode::Stealth ? "Stealth" : "Standard");

        auto modules = m_pm.GetModules();
        if (ImGui::BeginChild("ModuleList", ImVec2(0, 0), true)) {
            for (const auto& mod : modules) {
                if (ImGui::TreeNode(mod.name.c_str())) {
                    ImGui::Text("Base: 0x%llX", (unsigned long long)mod.baseAddress);
                    ImGui::Text("Size: 0x%zX", mod.imageSize);
                    ImGui::Text("Path: %s", mod.path.c_str());
                    if (ImGui::TreeNode("Sections")) {
                        for (const auto& sec : mod.sections) {
                            ImGui::Text("%-8s | 0x%llX | 0x%zX", sec.name.c_str(), (unsigned long long)sec.virtualAddress, sec.virtualSize);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();
        }
    }
}

void AppUI::RenderMemoryScannerTab() {
    const char* dataTypes[] = { "Int8", "Uint8", "Int16", "Uint16", "Int32", "Uint32", "Int64", "Uint64", "Float", "Double", "String", "AOB" };
    int currentDataType = (int)m_selectedDataType;
    if (ImGui::Combo("Data Type", &currentDataType, dataTypes, 12)) {
        m_selectedDataType = (Core::DataType)currentDataType;
    }

    const char* scanTypes[] = { "Exact Value", "Unknown Initial", "Increased", "Decreased", "Changed", "Unchanged", "Greater Than", "Less Than", "Between" };
    int currentScanType = (int)m_selectedScanType;
    if (ImGui::Combo("Scan Type", &currentScanType, scanTypes, 9)) {
        m_selectedScanType = (Core::ScanType)currentScanType;
    }

    ImGui::InputText("Value", m_scanValueBuf, sizeof(m_scanValueBuf));
    if (m_selectedScanType == Core::ScanType::Between) {
        ImGui::InputText("Value 2", m_scanValueBuf2, sizeof(m_scanValueBuf2));
    }

    if (ImGui::Button("First Scan")) {
        m_memScanner.FirstScan(GetCurrentScanValue(), m_selectedScanType);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Scan")) {
        m_memScanner.NextScan(GetCurrentScanValue(), m_selectedScanType);
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo")) {
        m_memScanner.Undo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_memScanner.Reset();
    }

    ImGui::Text("Results: %zu", m_memScanner.GetResultCount());

    if (ImGui::BeginChild("ScannerResults", ImVec2(0, 0), true)) {
        auto results = m_memScanner.GetResults();
        size_t displayCount = (std::min)(results.size(), (size_t)1000);
        for (size_t i = 0; i < displayCount; ++i) {
            ImGui::Text("0x%llX", (unsigned long long)results[i]);
        }
        if (results.size() > 1000) ImGui::Text("... and %zu more", results.size() - 1000);
        ImGui::EndChild();
    }
}

Core::ScanValue AppUI::GetCurrentScanValue() {
    Core::ScanValue sv;
    sv.type = m_selectedDataType;
    std::string s(m_scanValueBuf);
    std::string s2(m_scanValueBuf2);

    try {
        switch (m_selectedDataType) {
            case Core::DataType::Int8:   sv.value = (int8_t)std::stoi(s); break;
            case Core::DataType::Uint8:  sv.value = (uint8_t)std::stoul(s); break;
            case Core::DataType::Int16:  sv.value = (int16_t)std::stoi(s); break;
            case Core::DataType::Uint16: sv.value = (uint16_t)std::stoul(s); break;
            case Core::DataType::Int32:  sv.value = (int32_t)std::stol(s); break;
            case Core::DataType::Uint32: sv.value = (uint32_t)std::stoul(s); break;
            case Core::DataType::Int64:  sv.value = (int64_t)std::stoll(s); break;
            case Core::DataType::Uint64: sv.value = (uint64_t)std::stoull(s); break;
            case Core::DataType::Float:  sv.value = std::stof(s); break;
            case Core::DataType::Double: sv.value = std::stod(s); break;
            case Core::DataType::String: sv.value = s; break;
            case Core::DataType::AOB:    sv.value = s; break;
        }

        if (m_selectedScanType == Core::ScanType::Between) {
            switch (m_selectedDataType) {
                case Core::DataType::Int8:   sv.value2 = (int8_t)std::stoi(s2); break;
                case Core::DataType::Uint8:  sv.value2 = (uint8_t)std::stoul(s2); break;
                case Core::DataType::Int16:  sv.value2 = (int16_t)std::stoi(s2); break;
                case Core::DataType::Uint16: sv.value2 = (uint16_t)std::stoul(s2); break;
                case Core::DataType::Int32:  sv.value2 = (int32_t)std::stol(s2); break;
                case Core::DataType::Uint32: sv.value2 = (uint32_t)std::stoul(s2); break;
                case Core::DataType::Int64:  sv.value2 = (int64_t)std::stoll(s2); break;
                case Core::DataType::Uint64: sv.value2 = (uint64_t)std::stoull(s2); break;
                case Core::DataType::Float:  sv.value2 = std::stof(s2); break;
                case Core::DataType::Double: sv.value2 = std::stod(s2); break;
                default: break;
            }
        }
    } catch (...) {}
    return sv;
}

void AppUI::RenderSignatureTab() {
    if (ImGui::Button("Run Signatures Scan")) {
        m_status = "Scanning...";
        bool isVulkan = (std::string(m_processName).find("Vulkan") != std::string::npos);
        m_scanner.Run(m_sigs, isVulkan);
        m_status = "Scan Complete";
    }
    ImGui::SameLine();
    if (ImGui::Button("Export offsets.h")) {
        ExportToHeader();
        m_status = "Exported to offsets.h";
    }

    if (ImGui::BeginTable("sigresults", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Offset");
        ImGui::TableHeadersRow();

        for (const auto& sig : m_sigs) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", sig.name.c_str());

            ImGui::TableSetColumnIndex(1);
            if (sig.result) {
                ImGui::Text("0x%llX", (unsigned long long)sig.result);
                ImGui::TableSetColumnIndex(2);
                uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
                ImGui::Text("0x%llX", (unsigned long long)(sig.result - base));
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Not Found");
            }
        }
        ImGui::EndTable();
    }
}

void AppUI::RenderPointerScanTab() {
    static char targetAddrStr[32] = "";
    ImGui::InputText("Target Address", targetAddrStr, sizeof(targetAddrStr));
    ImGui::InputInt("Max Depth", &m_ptrDepth);
    ImGui::InputInt("Max Offset", &m_ptrOffset);

    if (ImGui::Button("Start Pointer Scan")) {
        try {
            m_ptrTarget = std::stoull(targetAddrStr, nullptr, 16);
            m_ptrScanner.StartScan(m_ptrTarget, m_ptrDepth, m_ptrOffset);
        } catch (...) {
            m_status = "Invalid target address";
        }
    }

    if (m_ptrScanner.IsScanning()) {
        ImGui::SameLine();
        ImGui::Text("Scanning...");
        if (ImGui::Button("Cancel Scan")) m_ptrScanner.Cancel();
    }

    if (ImGui::BeginChild("PtrResults", ImVec2(0, 0), true)) {
        auto results = m_ptrScanner.GetResults();
        for (const auto& chain : results) {
            std::stringstream ss;
            ss << chain.moduleName << "+0x" << std::hex << chain.offsets[0];
            for (size_t i = 1; i < chain.offsets.size(); ++i) {
                ss << " -> 0x" << std::hex << chain.offsets[i];
            }
            ImGui::Text("%s", ss.str().c_str());
        }
        ImGui::EndChild();
    }
}

void AppUI::RenderDumperTab() {
    ImGui::InputScalar("Base Address", ImGuiDataType_U64, &m_structBase, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::InputText("Struct Name", m_structName, sizeof(m_structName));

    if (ImGui::Button("Add Field")) {
        m_structFields.push_back({"Field_" + std::to_string(m_structFields.size()), "uintptr_t", 0});
    }

    for (size_t i = 0; i < m_structFields.size(); ++i) {
        char nameId[32]; sprintf(nameId, "Name##%zu", i);
        char offId[32]; sprintf(offId, "Off##%zu", i);
        char typeId[32]; sprintf(typeId, "Type##%zu", i);

        ImGui::PushItemWidth(100);
        char fieldName[64]; strcpy(fieldName, m_structFields[i].name.c_str());
        if (ImGui::InputText(nameId, fieldName, sizeof(fieldName))) m_structFields[i].name = fieldName;
        ImGui::SameLine();
        ImGui::InputScalar(offId, ImGuiDataType_U64, &m_structFields[i].offset, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        const char* types[] = { "uintptr_t", "int32", "uint32", "float" };
        int currentType = 0;
        for (int k = 0; k < 4; ++k) if (m_structFields[i].type == types[k]) currentType = k;
        if (ImGui::Combo(typeId, &currentType, types, 4)) m_structFields[i].type = types[currentType];
        ImGui::PopItemWidth();
    }

    if (ImGui::Button("Dump Structure")) {
        Core::StructDefinition def = { m_structName, m_structFields };
        auto results = m_dumper.DumpStructure(m_structBase, def);
        m_dumper.SaveToJSON(std::string(m_structName) + ".json", results);
        m_status = "Dumped to " + std::string(m_structName) + ".json";
    }
}

void AppUI::RenderHexViewerTab() {
    static uintptr_t hexBase = 0;
    ImGui::InputScalar("View Address", ImGuiDataType_U64, &hexBase, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);

    uint8_t buffer[256];
    if (m_pm.ReadMemory(hexBase, buffer, sizeof(buffer))) {
        for (int i = 0; i < 16; ++i) {
            ImGui::Text("0x%llX: ", (unsigned long long)(hexBase + i * 16));
            ImGui::SameLine();
            for (int j = 0; j < 16; ++j) {
                ImGui::Text("%02X ", buffer[i * 16 + j]);
                ImGui::SameLine();
            }
            ImGui::Text(" | ");
            ImGui::SameLine();
            for (int j = 0; j < 16; ++j) {
                char c = buffer[i * 16 + j];
                ImGui::Text("%c", (c >= 32 && c <= 126) ? c : '.');
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
    }
}

void AppUI::ExportToHeader() {
    std::ofstream f("offsets.h");
    f << "#pragma once\n\n";
    f << "namespace Offsets {\n";
    for (const auto& sig : m_sigs) {
        if (sig.result) {
            uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
            f << "    constexpr unsigned long long " << sig.name << " = 0x"
              << std::hex << std::uppercase << (sig.result - base) << ";\n";
        }
    }
    f << "}\n";
}

} // namespace UI
