#include "UI/AppUI.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace UI {

AppUI::AppUI(Core::ProcessManager& pm, Core::Scanner& scanner)
    : m_pm(pm), m_scanner(scanner), m_memScanner(pm), m_ptrScanner(pm), m_dumper(pm) {
    m_sigs = m_scanner.LoadSignatures("signatures.json");
    SetupStyles();
}

void AppUI::SetupStyles() {
    auto& style = ImGui::GetStyle();

    // Adjust for high-DPI and touch targets
    float scale = 1.2f; // Base scale factor
    style.WindowPadding = ImVec2(10, 10) * scale;
    style.FramePadding = ImVec2(8, 6) * scale;
    style.ItemSpacing = ImVec2(10, 8) * scale;
    style.ScrollbarSize = 14 * scale;
    style.GrabMinSize = 12 * scale;

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;

    // Semantic Colors (Dark Theme Optimized)
    auto* colors = style.Colors;
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.30f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
}

void AppUI::PushStatusColor(const std::string& status, bool success) {
    m_status = status;
    if (status.find("Failed") != std::string::npos || status.find("Invalid") != std::string::npos || status.find("Error") != std::string::npos) {
        m_statusColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Error Red
    } else if (status.find("Attached") != std::string::npos || status.find("Complete") != std::string::npos || status.find("Success") != std::string::npos) {
        m_statusColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // Success Green
    } else if (status.find("Scanning") != std::string::npos || status.find("Working") != std::string::npos) {
        m_statusColor = ImVec4(0.3f, 0.6f, 1.0f, 1.0f); // Info Blue
    } else {
        m_statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Neutral Gray
    }
}

void AppUI::Render() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Universal Offset Dumper", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    RenderHeader();
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
        ImGuiTabItemFlags flags = 0;
        if (m_activeTab == 0) flags |= ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem("Process", nullptr, flags)) {
            RenderProcessTab();
            ImGui::EndTabItem();
            m_activeTab = -1;
        }

        auto RenderTab = [&](const char* name, int index, auto func) {
            bool attached = m_isAttached;
            if (!attached) ImGui::BeginDisabled();

            ImGuiTabItemFlags t_flags = 0;
            if (m_activeTab == index) t_flags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(name, nullptr, t_flags)) {
                func();
                ImGui::EndTabItem();
                m_activeTab = -1;
            }
            if (!attached) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Attachment required to access this feature.");
                }
            }
        };

        RenderTab("Memory Scanner", 1, [&]() { RenderMemoryScannerTab(); });
        RenderTab("Signature Scanner", 2, [&]() { RenderSignatureTab(); });
        RenderTab("Pointer Scan", 3, [&]() { RenderPointerScanTab(); });
        RenderTab("Structure Dumper", 4, [&]() { RenderDumperTab(); });
        RenderTab("Hex Viewer", 5, [&]() { RenderHexViewerTab(); });

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void AppUI::RenderHeader() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 0));

    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Universal Offset Dumper v1.0");
    ImGui::SameLine();

    if (m_isAttached) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "● ATTACHED: %s (%d)", m_processName, m_pm.GetPid());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "○ DISCONNECTED");
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 450);

    if (m_memScanner.IsScanning() || m_ptrScanner.IsScanning()) {
        bool isMem = m_memScanner.IsScanning();
        ImGui::Text("%s:", isMem ? "Mem Scan" : "Ptr Scan");
        ImGui::SameLine();
        ImGui::ProgressBar(isMem ? m_memScanner.GetProgress() : m_ptrScanner.GetProgress(), ImVec2(150, 0));
        ImGui::SameLine();
    }

    ImGui::Text("Status:");
    ImGui::SameLine();
    ImGui::TextColored(m_statusColor, "%s", m_status.c_str());

    ImGui::PopStyleVar();
}

void AppUI::RenderProcessPicker() {
    float now = (float)ImGui::GetTime();
    if (m_cachedProcesses.empty() || now - m_lastProcRefresh > 5.0f) {
        m_cachedProcesses = m_pm.GetProcessList();
        m_lastProcRefresh = now;
    }

    ImGui::Text("Filter Processes:");
    ImGui::InputText("##procfilter", m_procFilter, sizeof(m_procFilter));

    ImGui::BeginChild("ProcPickerList", ImVec2(0, 300), true);
    ImGuiListClipper clipper;

    std::vector<Core::ProcessInfo> filtered;
    for (const auto& proc : m_cachedProcesses) {
        if (strlen(m_procFilter) == 0 || std::string(proc.name).find(m_procFilter) != std::string::npos) {
            filtered.push_back(proc);
        }
    }

    clipper.Begin((int)filtered.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const auto& proc = filtered[i];
            char label[128];
            sprintf(label, "[%5d] %s", proc.pid, proc.name.c_str());
            if (ImGui::Selectable(label, strcmp(m_processName, proc.name.c_str()) == 0)) {
                strcpy(m_processName, proc.name.c_str());
            }
        }
    }
    ImGui::EndChild();
}

void AppUI::RenderProcessTab() {
    ImGui::Columns(2, "proc_cols", false);
    ImGui::SetColumnWidth(0, 400);

    RenderProcessPicker();

    ImGui::NextColumn();

    ImGui::Text("Selected: %s", m_processName);
    ImGui::Dummy(ImVec2(0, 10));

    if (ImGui::Button("Attach (Standard)", ImVec2(-1, 40))) {
        if (m_pm.Attach(m_processName, Core::MemoryMode::Standard)) {
            m_isAttached = true;
            PushStatusColor("Attached (Standard)");
            m_memScanner.Reset();
        } else {
            PushStatusColor("Failed to Attach (Standard)");
        }
    }

    bool driverLoaded = m_pm.IsDriverLoaded();
    if (!driverLoaded) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Attach (Stealth)", ImVec2(-1, 40))) {
        if (m_pm.Attach(m_processName, Core::MemoryMode::Stealth)) {
            m_isAttached = true;
            PushStatusColor("Attached (Stealth Mode)");
            m_memScanner.Reset();
        } else {
            PushStatusColor("Failed to Attach Stealth (Driver Issue?)");
        }
    }

    if (!driverLoaded) {
        ImGui::EndDisabled();
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Kernel Driver not loaded.");
        ImGui::TextDisabled("Stealth mode requires 'KernelDumper.sys'");
    } else {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Stealth Driver Verified: Ready.");
    }

    ImGui::Columns(1);

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
        if (results.empty()) {
            RenderEmptyState("No scan results found.", "Try a different value or scan type.");
        } else {
            ImGuiListClipper clipper;
            clipper.Begin((int)results.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    char label[32];
                    sprintf(label, "0x%llX", (unsigned long long)results[i]);
                    if (ImGui::Selectable(label)) {
                        // Jump to Hex Viewer
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        m_hexBase = results[i];
                        sprintf(m_hexAddrBuf, "%llX", (unsigned long long)m_hexBase);
                        m_activeTab = 5; // Hex Viewer index
                    }
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Jump to in Hex Viewer")) {
                            m_hexBase = results[i];
                            sprintf(m_hexAddrBuf, "%llX", (unsigned long long)m_hexBase);
                            m_activeTab = 5;
                        }
                        if (ImGui::MenuItem("Copy Address")) {
                            ImGui::SetClipboardText(label);
                        }
                        ImGui::EndPopup();
                    }
                }
            }
        }
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
    if (ImGui::Button("Export offsets.h", ImVec2(150, 0))) {
        ExportToHeader();
        PushStatusColor("Exported to offsets.h");
    }

    ImGui::SameLine();
    if (ImGui::BeginCombo("##export_presets", "Export Presets...", ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("C++ Header (.h)")) {
            ExportToHeader();
            PushStatusColor("Exported to offsets.h");
        }
        if (ImGui::Selectable("C# Class (.cs)")) {
            std::ofstream f("Offsets.cs");
            f << "public static class Offsets {\n";
            for (const auto& sig : m_sigs) {
                if (sig.result) {
                    uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
                    f << "    public const long " << sig.name << " = 0x" << std::hex << std::uppercase << (sig.result - base) << ";\n";
                }
            }
            f << "}\n";
            PushStatusColor("Exported to Offsets.cs");
        }
        if (ImGui::Selectable("Rust Module (.rs)")) {
            std::ofstream f("offsets.rs");
            f << "pub mod offsets {\n";
            for (const auto& sig : m_sigs) {
                if (sig.result) {
                    uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
                    f << "    pub const " << sig.name << ": usize = 0x" << std::hex << std::uppercase << (sig.result - base) << ";\n";
                }
            }
            f << "}\n";
            PushStatusColor("Exported to offsets.rs");
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Export offsets.txt")) {
        std::vector<Core::OffsetResult> results;
        for (const auto& sig : m_sigs) {
            if (sig.result) {
                uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
                std::stringstream ss;
                ss << std::hex << std::uppercase << sig.result;
                results.push_back({ sig.result - base, sig.moduleName, sig.name, "Offset", ss.str() });
            }
        }
        m_dumper.SaveToText("offsets.txt", m_processName, m_pm.GetPid(), results);
        m_status = "Exported to offsets.txt";
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
                char addrStr[32]; sprintf(addrStr, "0x%llX", (unsigned long long)sig.result);
                ImGui::Text("%s", addrStr);
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Copy Address")) ImGui::SetClipboardText(addrStr);
                    if (ImGui::MenuItem("Jump to in Hex Viewer")) {
                        m_hexBase = sig.result;
                        sprintf(m_hexAddrBuf, "%llX", (unsigned long long)m_hexBase);
                        m_activeTab = 5;
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(2);
                uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
                char offStr[32]; sprintf(offStr, "0x%llX", (unsigned long long)(sig.result - base));
                ImGui::Text("%s", offStr);
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Copy Offset")) ImGui::SetClipboardText(offStr);
                    ImGui::EndPopup();
                }
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
        if (results.empty()) {
            RenderEmptyState("No pointer chains found.", "Try increasing Max Depth or Max Offset.");
        } else {
            for (const auto& chain : results) {
                std::stringstream ss;
                ss << chain.moduleName << "+0x" << std::hex << chain.offsets[0];
                for (size_t i = 1; i < chain.offsets.size(); ++i) {
                    ss << " -> 0x" << std::hex << chain.offsets[i];
                }
                std::string chainStr = ss.str();
                if (ImGui::Selectable(chainStr.c_str())) {
                    ImGui::SetClipboardText(chainStr.c_str());
                    PushStatusColor("Pointer chain copied to clipboard!");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to copy pointer chain");
                }
            }
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

    ImGui::Separator();
    ImGui::Text("Automated Data Section Analysis");
    static char targetModule[64] = "RainbowSix.exe";
    ImGui::InputText("Module Name##Dumper", targetModule, sizeof(targetModule));

    if (ImGui::Button("Analyze Data Sections")) {
        auto results = m_dumper.AnalyzeDataSections(targetModule);
        m_dumper.SaveToJSON("data_analysis.json", results);
        m_status = "Analysis complete. Results in data_analysis.json";
    }
}

void AppUI::RenderEmptyState(const char* message, const char* suggestion) {
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 50.0f);
    ImGui::BeginGroup();

    // Center text
    float windowWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(message).x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", message);

    textWidth = ImGui::CalcTextSize(suggestion).x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::TextDisabled("%s", suggestion);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SetCursorPosX((windowWidth - 120) * 0.5f);
    if (ImGui::Button("Reset Scan", ImVec2(120, 30))) {
        m_memScanner.Reset();
    }

    ImGui::EndGroup();
}

void AppUI::RenderHexViewerTab() {
    ImGui::Text("Address:");
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    if (ImGui::InputText("##HexAddr", m_hexAddrBuf, sizeof(m_hexAddrBuf), ImGuiInputTextFlags_CharsHexadecimal)) {
        try { m_hexBase = std::stoull(m_hexAddrBuf, nullptr, 16); } catch (...) {}
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        try { m_hexBase = std::stoull(m_hexAddrBuf, nullptr, 16); } catch (...) {}
    }

    ImGui::Separator();

    if (ImGui::BeginChild("HexScroll", ImVec2(0, 0), true)) {
        const int rows = 32;
        uint8_t buffer[rows * 16];
        if (m_pm.ReadMemory(m_hexBase, buffer, sizeof(buffer))) {
            for (int i = 0; i < rows; ++i) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "0x%012llX: ", (unsigned long long)(m_hexBase + i * 16));
                ImGui::SameLine();

                for (int j = 0; j < 16; ++j) {
                    uint8_t b = buffer[i * 16 + j];
                    if (b == 0) ImGui::TextDisabled("00 ");
                    else if (b >= 32 && b <= 126) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%02X ", b);
                    else if (b == 0xFF) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%02X ", b);
                    else ImGui::Text("%02X ", b);
                    ImGui::SameLine();
                }

                ImGui::Text("| ");
                ImGui::SameLine();

                for (int j = 0; j < 16; ++j) {
                    char c = buffer[i * 16 + j];
                    if (c >= 32 && c <= 126) ImGui::Text("%c", c);
                    else ImGui::TextDisabled(".");
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Cannot read memory at 0x%llX", (unsigned long long)m_hexBase);
        }
        ImGui::EndChild();
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
