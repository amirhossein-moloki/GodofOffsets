#include "UI/AppUI.h"
#include "Utils/FormatUtils.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <map>
#include <algorithm>

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
    ImGui::GetIO().FontGlobalScale = scale;

    style.WindowPadding = ImVec2(10 * scale, 10 * scale);
    style.FramePadding = ImVec2(8 * scale, 6 * scale);
    style.ItemSpacing = ImVec2(10 * scale, 8 * scale);
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
    if (m_showDisclaimer) {
        ImGui::OpenPopup("Security Disclaimer");
    }

    if (ImGui::BeginPopupModal("Security Disclaimer", &m_showDisclaimer, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("WARNING: SECURITY RESEARCH TOOL");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextWrapped("This tool is intended for educational and security research purposes only.");
        ImGui::TextWrapped("Using this tool on games with active anti-cheat systems (e.g., BattlEye, EAC) may result in an account ban.");
        ImGui::TextWrapped("The developers assume no responsibility for any misuse or damage caused by this software.");
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        if (ImGui::Button("I Understand", ImVec2(120, 0))) {
            m_showDisclaimer = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Handle Global Keyboard Shortcuts
    auto& io = ImGui::GetIO();
    if (io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_P)) m_activeTab = TabID::Process;
        if (ImGui::IsKeyPressed(ImGuiKey_M) && m_isAttached) m_activeTab = TabID::MemoryScanner;
        if (ImGui::IsKeyPressed(ImGuiKey_S) && m_isAttached) m_activeTab = TabID::SignatureScanner;
        if (ImGui::IsKeyPressed(ImGuiKey_T) && m_isAttached) m_activeTab = TabID::PointerScanner;
        if (ImGui::IsKeyPressed(ImGuiKey_D) && m_isAttached) m_activeTab = TabID::Dumper;
        if (ImGui::IsKeyPressed(ImGuiKey_H) && m_isAttached) m_activeTab = TabID::HexViewer;
        if (ImGui::IsKeyPressed(ImGuiKey_G)) m_activeTab = TabID::ActivityLog;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Universal Offset Dumper", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    RenderHeader();
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
        ImGuiTabItemFlags flags = 0;
        if (m_activeTab == TabID::Process) flags |= ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem("[P] Process", nullptr, flags)) {
            RenderProcessTab();
            ImGui::EndTabItem();
            m_activeTab = TabID::None;
        }

        auto RenderTab = [&](const char* name, TabID index, auto func) {
            bool attached = m_isAttached;
            if (!attached && index != TabID::ActivityLog) ImGui::BeginDisabled();

            ImGuiTabItemFlags t_flags = 0;
            if (m_activeTab == index) t_flags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(name, nullptr, t_flags)) {
                func();
                ImGui::EndTabItem();
                m_activeTab = TabID::None;
            }
            if (!attached) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Attachment required to access this feature.");
                }
            }
        };

        RenderTab("[M] Memory Scanner", TabID::MemoryScanner, [&]() { RenderMemoryScannerTab(); });
        RenderTab("[S] Signature Scanner", TabID::SignatureScanner, [&]() { RenderSignatureTab(); });
        RenderTab("[T] Pointer Scan", TabID::PointerScanner, [&]() { RenderPointerScanTab(); });
        RenderTab("[D] Structure Dumper", TabID::Dumper, [&]() { RenderDumperTab(); });
        RenderTab("[H] Hex Viewer", TabID::HexViewer, [&]() { RenderHexViewerTab(); });
        RenderTab("[G] Activity Log", TabID::ActivityLog, [&]() { RenderActivityLogTab(); });

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

    if (!m_recentProcesses.empty()) {
        ImGui::Text("Recent Processes:");
        for (const auto& recent : m_recentProcesses) {
            ImGui::SameLine();
            if (ImGui::SmallButton(recent.c_str())) {
                size_t len = recent.copy(m_processName, sizeof(m_processName) - 1);
                m_processName[len] = '\0';
            }
        }
        ImGui::Dummy(ImVec2(0, 5));
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
            std::string label = "[" + std::to_string(proc.pid) + "] " + proc.name + " (" + (proc.is64Bit ? "x64" : "x86") + ")";
            if (ImGui::Selectable(label.c_str(), strcmp(m_processName, proc.name.c_str()) == 0)) {
                size_t len = proc.name.copy(m_processName, sizeof(m_processName) - 1);
                m_processName[len] = '\0';
            }
        }
    }
    ImGui::EndChild();
}

void AppUI::RenderProcessTab() {
    ImGui::Columns(2, "proc_cols", false);
    ImGui::SetColumnWidth(0, 450);

    RenderProcessPicker();

    ImGui::NextColumn();

    ImGui::BeginGroup();
    ImGui::Text("TARGET SELECTION");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    ImGui::Text("Selected Process:");
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Assuming index 0 is default/bold if available
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "  %s", m_processName[0] ? m_processName : "None");
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Text("ATTACHMENT MODE");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stealth Mode requires the 'KernelDumper.sys' driver in the same directory\nand the application to be running with Administrator privileges.");
    }
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    ImGui::PushStyleColor(ImGuiCol_Button, m_primaryColor);
    if (ImGui::Button("Attach (Standard Mode)", ImVec2(-1, 45))) {
        if (m_pm.Attach(m_processName, Core::MemoryMode::Standard)) {
            m_isAttached = true;
            AddToRecentProcesses(m_processName);

            PushStatusColor("Attached (Standard)");
            AddLog("Successfully attached to " + std::string(m_processName) + " (Standard Mode)", LogSeverity::Success);
            m_memScanner.Reset();
        } else {
            PushStatusColor("Failed to Attach (Standard)");
            AddLog("Failed to attach to " + std::string(m_processName) + " in Standard Mode", LogSeverity::Error);
        }
    }
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Uses standard Win32 APIs. Detectable by Anti-Cheats.");

    ImGui::Dummy(ImVec2(0, 15));

    bool driverLoaded = m_pm.IsDriverLoaded();
    if (!driverLoaded) ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.2f, 0.6f, 1.0f)); // Purple for Stealth
    if (ImGui::Button("Attach (Stealth Mode)", ImVec2(-1, 45))) {
        if (m_pm.Attach(m_processName, Core::MemoryMode::Stealth)) {
            m_isAttached = true;
            AddToRecentProcesses(m_processName);

            PushStatusColor("Attached (Stealth Mode)");
            AddLog("Successfully attached to " + std::string(m_processName) + " (Stealth Mode)", LogSeverity::Success);
            m_memScanner.Reset();
        } else {
            PushStatusColor("Failed to Attach Stealth (Driver Issue?)");
            AddLog("Failed to attach to " + std::string(m_processName) + " in Stealth Mode. Check driver status.", LogSeverity::Error);
        }
    }
    ImGui::PopStyleColor();

    if (!driverLoaded) {
        ImGui::EndDisabled();
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "● Stealth Mode Unavailable");
        ImGui::TextWrapped("The kernel driver 'KernelDumper.sys' was not found or failed to load. Ensure the driver is in the same directory and you are running as Administrator.");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1), "● Stealth Driver Verified: READY");
        ImGui::TextDisabled("Uses Ring-0 memory operations to bypass User-mode hooks.");
    }
    ImGui::EndGroup();

    ImGui::Columns(1);

    if (m_isAttached) {
        ImGui::Separator();
        ImGui::Text("PID: %d | Mode: %s", m_pm.GetPid(), m_pm.GetMode() == Core::MemoryMode::Stealth ? "Stealth" : "Standard");

        auto modules = m_pm.GetModules();
        if (ImGui::BeginChild("ModuleList", ImVec2(0, 0), true)) {
            for (const auto& mod : modules) {
                if (ImGui::TreeNode(mod.name.c_str())) {
                    ImGui::Text("Base: 0x%llX", (unsigned long long)mod.baseAddress);
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Jump to Base in Hex Viewer")) {
                            JumpToHex(mod.baseAddress);
                        }
                        if (ImGui::MenuItem("Copy Base Address")) {
                            char buf[32]; sprintf(buf, "0x%llX", (unsigned long long)mod.baseAddress);
                            ImGui::SetClipboardText(buf);
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::Text("Size: 0x%zX", mod.imageSize);
                    ImGui::Text("Path: %s", mod.path.c_str());
                    if (ImGui::TreeNode("Sections")) {
                        for (const auto& sec : mod.sections) {
                            ImGui::Text("%-8s | 0x%llX | 0x%zX", sec.name.c_str(), (unsigned long long)sec.virtualAddress, sec.virtualSize);
                            if (ImGui::BeginPopupContextItem()) {
                                if (ImGui::MenuItem("Jump to Section in Hex Viewer")) {
                                    JumpToHex(sec.virtualAddress);
                                }
                                ImGui::EndPopup();
                            }
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

    ImGui::Text("Scan Settings");
    ImGui::Separator();

    ImGui::PushItemWidth(200);
    if (ImGui::BeginCombo("Scan Type", scanTypes[(int)m_selectedScanType])) {
        auto SelectableScanType = [&](Core::ScanType type) {
            if (ImGui::Selectable(scanTypes[(int)type], m_selectedScanType == type)) {
                m_selectedScanType = type;
            }
        };

        ImGui::SeparatorText("Search Methods");
        SelectableScanType(Core::ScanType::ExactValue);
        SelectableScanType(Core::ScanType::UnknownInitial);
        SelectableScanType(Core::ScanType::Between);

        ImGui::SeparatorText("Value Filtering");
        SelectableScanType(Core::ScanType::Increased);
        SelectableScanType(Core::ScanType::Decreased);
        SelectableScanType(Core::ScanType::Changed);
        SelectableScanType(Core::ScanType::Unchanged);
        SelectableScanType(Core::ScanType::GreaterThan);
        SelectableScanType(Core::ScanType::LessThan);

        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::PushItemWidth(250);
    ImGui::InputText("Primary Value", m_scanValueBuf, sizeof(m_scanValueBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_selectedScanType == Core::ScanType::Between) {
        ImGui::InputText("Secondary Value", m_scanValueBuf2, sizeof(m_scanValueBuf2), ImGuiInputTextFlags_EnterReturnsTrue);
    }
    ImGui::PopItemWidth();

    ImGui::Checkbox("Modify Memory Protection (Loud)", &m_modifyProtection);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Temporarily changes memory protection to PAGE_EXECUTE_READWRITE to read protected regions.\nUse with caution as this can be detected by Anti-Cheats.");

    ImGui::Dummy(ImVec2(0, 10));

    ImGui::PushStyleColor(ImGuiCol_Button, m_primaryColor);
    if (ImGui::Button("First Scan", ImVec2(120, 35))) {
        AddLog("Starting First Scan...", LogSeverity::Info);
        m_memScanner.FirstScan(GetCurrentScanValue(), m_selectedScanType, m_modifyProtection);
        AddLog("First Scan complete. Results: " + std::to_string(m_memScanner.GetResultCount()), LogSeverity::Success);
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, m_accentColor);
    if (ImGui::Button("Next Scan", ImVec2(120, 35))) {
        AddLog("Starting Next Scan...", LogSeverity::Info);
        m_memScanner.NextScan(GetCurrentScanValue(), m_selectedScanType, m_modifyProtection);
        AddLog("Next Scan complete. Results: " + std::to_string(m_memScanner.GetResultCount()), LogSeverity::Success);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Undo")) {
        m_memScanner.Undo();
        AddLog("Undo performed. Results: " + std::to_string(m_memScanner.GetResultCount()), LogSeverity::Info);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_memScanner.Reset();
        AddLog("Memory scanner reset.", LogSeverity::Warning);
    }
    ImGui::PopStyleColor();

    ImGui::Text("Results: %zu", m_memScanner.GetResultCount());

    if (ImGui::BeginChild("ScannerResults", ImVec2(0, 0), true)) {
        auto results = m_memScanner.GetResults();
        auto snapshotValues = m_memScanner.GetResultValues();

        if (results.empty()) {
            RenderEmptyState("No scan results found.", "Try a different value or scan type.");
        } else {
            if (ImGui::BeginTable("ScannerResultsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Snapshot", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Live Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin((int)results.size());

                size_t typeSize = 0;
                if (!results.empty()) {
                    typeSize = snapshotValues.size() / results.size();
                }

                auto FormatValue = [&](const uint8_t* ptr, size_t size, char* outStr) {
                    switch (m_selectedDataType) {
                        case Core::DataType::Int8:   sprintf(outStr, "%d", *(int8_t*)ptr); break;
                        case Core::DataType::Uint8:  sprintf(outStr, "%u", *(uint8_t*)ptr); break;
                        case Core::DataType::Int16:  sprintf(outStr, "%d", *(int16_t*)ptr); break;
                        case Core::DataType::Uint16: sprintf(outStr, "%u", *(uint16_t*)ptr); break;
                        case Core::DataType::Int32:  sprintf(outStr, "%d", *(int32_t*)ptr); break;
                        case Core::DataType::Uint32: sprintf(outStr, "%u", *(uint32_t*)ptr); break;
                        case Core::DataType::Int64:  sprintf(outStr, "%lld", *(int64_t*)ptr); break;
                        case Core::DataType::Uint64: sprintf(outStr, "%llu", *(uint64_t*)ptr); break;
                        case Core::DataType::Float:  sprintf(outStr, "%.4f", *(float*)ptr); break;
                        case Core::DataType::Double: sprintf(outStr, "%.8f", *(double*)ptr); break;
                        case Core::DataType::String: {
                            size_t len = (std::min)(size, (size_t)63);
                            memcpy(outStr, ptr, len);
                            outStr[len] = '\0';
                            break;
                        }
                        case Core::DataType::AOB: {
                            outStr[0] = '\0';
                            for (size_t k = 0; k < (std::min)(size, (size_t)16); ++k) {
                                char b[4]; sprintf(b, "%02X ", ptr[k]);
                                strcat(outStr, b);
                            }
                            if (size > 16) strcat(outStr, "...");
                            break;
                        }
                    }
                };

                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);

                        std::string addrStr = Utils::ToHex(results[i]);

                        bool selected = false;
                        if (ImGui::Selectable(addrStr.c_str(), &selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            // Selection logic
                        }

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                            JumpToHex(results[i]);
                        }

                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Jump to in Hex Viewer")) JumpToHex(results[i]);
                            if (ImGui::MenuItem("Copy Address")) ImGui::SetClipboardText(addrStr.c_str());
                            ImGui::EndPopup();
                        }

                        // Snapshot Column
                        ImGui::TableSetColumnIndex(1);
                        if (i * typeSize < snapshotValues.size()) {
                            char valStr[128];
                            FormatValue(&snapshotValues[i * typeSize], typeSize, valStr);
                            ImGui::TextDisabled("%s", valStr);
                        }

                        // Live Value Column
                        ImGui::TableSetColumnIndex(2);
                        std::vector<uint8_t> liveBuf(typeSize);
                        if (m_pm.ReadMemory(results[i], liveBuf.data(), typeSize)) {
                            char liveStr[128];
                            FormatValue(liveBuf.data(), typeSize, liveStr);

                            // Highlight if changed
                            bool changed = false;
                            if (i * typeSize < snapshotValues.size()) {
                                changed = memcmp(liveBuf.data(), &snapshotValues[i * typeSize], typeSize) != 0;
                            }

                            if (changed) ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", liveStr);
                            else ImGui::Text("%s", liveStr);
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "?? (Unreadable)");
                        }
                    }
                }
                ImGui::EndTable();
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
            case Core::DataType::String16: sv.value = Utils::ToUTF16LE(s); break;
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
    ImGui::PushStyleColor(ImGuiCol_Button, m_primaryColor);
    if (ImGui::Button("Run Signatures Scan", ImVec2(200, 35))) {
        AddLog("Starting signature scan...", LogSeverity::Info);
        m_status = "Scanning...";
        bool isVulkan = (std::string(m_processName).find("Vulkan") != std::string::npos);
        m_scanner.Run(m_sigs, isVulkan);
        m_status = "Scan Complete";
        AddLog("Signature scan complete.", LogSeverity::Success);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Export offsets.h", ImVec2(150, 0))) {
        ExportToHeader();
        PushStatusColor("Exported to offsets.h");
        AddLog("Offsets exported to offsets.h", LogSeverity::Success);
    }

    ImGui::SameLine();
    if (ImGui::BeginCombo("##export_presets", "Export Presets...", ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("C++ Header (.h)")) {
            ExportToHeader();
            PushStatusColor("Exported to offsets.h");
            AddLog("Offsets exported to offsets.h", LogSeverity::Success);
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
            AddLog("Offsets exported to Offsets.cs", LogSeverity::Success);
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
            AddLog("Offsets exported to offsets.rs", LogSeverity::Success);
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
        AddLog("Offsets exported to offsets.txt", LogSeverity::Success);
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
                std::string addrStr = Utils::ToHex(sig.result);
                ImGui::Text("%s", addrStr.c_str());
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Copy Address")) ImGui::SetClipboardText(addrStr.c_str());
                    if (ImGui::MenuItem("Jump to in Hex Viewer")) {
                        JumpToHex(sig.result);
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(2);
                uintptr_t base = m_pm.GetModuleBase(sig.moduleName);
                std::string offStr = Utils::ToHex(sig.result - base);
                ImGui::Text("%s", offStr.c_str());
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Copy Offset")) ImGui::SetClipboardText(offStr.c_str());
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

    ImGui::PushStyleColor(ImGuiCol_Button, m_primaryColor);
    if (ImGui::Button("Start Pointer Scan", ImVec2(200, 35))) {
        try {
            m_ptrTarget = std::stoull(targetAddrStr, nullptr, 16);
            AddLog("Starting pointer scan for address: 0x" + std::string(targetAddrStr), LogSeverity::Info);
            m_ptrScanner.StartScan(m_ptrTarget, m_ptrDepth, m_ptrOffset);
        } catch (...) {
            m_status = "Invalid target address";
            AddLog("Invalid target address for pointer scan: " + std::string(targetAddrStr), LogSeverity::Error);
        }
    }
    ImGui::PopStyleColor();

    if (m_ptrScanner.IsScanning()) {
        ImGui::SameLine();
        ImGui::Text("Scanning...");
        if (ImGui::Button("Cancel Scan")) {
            m_ptrScanner.Cancel();
            AddLog("Pointer scan cancellation requested.", LogSeverity::Warning);
        }
    }

    if (ImGui::BeginChild("PtrResults", ImVec2(0, 0), true)) {
        if (m_ptrScanner.IsScanning()) {
            m_groupedPtrResults.clear(); // Clear during active scan
        } else if (m_groupedPtrResults.empty()) {
            auto results = m_ptrScanner.GetResults();
            if (!results.empty()) {
                for (const auto& chain : results) {
                    m_groupedPtrResults[chain.moduleName][chain.offsets[0]].push_back(chain);
                }
            }
        }

        if (m_groupedPtrResults.empty() && !m_ptrScanner.IsScanning()) {
            RenderEmptyState("No pointer chains found.", "Try increasing Max Depth or Max Offset.");
        } else {
            for (auto& modPair : m_groupedPtrResults) {
                if (ImGui::TreeNodeEx(modPair.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (auto& basePair : modPair.second) {
                        char baseLabel[64];
                        sprintf(baseLabel, "Base Offset: 0x%llX (%zu chains)", (unsigned long long)basePair.first, basePair.second.size());
                        if (ImGui::TreeNode(baseLabel)) {
                            for (const auto& chain : basePair.second) {
                                std::stringstream ss;
                                ss << "Offsets: ";
                                for (size_t i = 0; i < chain.offsets.size(); ++i) {
                                    ss << (i == 0 ? "" : " -> ") << "0x" << std::hex << std::uppercase << chain.offsets[i];
                                }
                                std::string chainStr = ss.str();
                                if (ImGui::Selectable(chainStr.c_str())) {
                                    ImGui::SetClipboardText(chainStr.c_str());
                                    AddLog("Copied pointer chain to clipboard.", LogSeverity::Info);
                                }
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy full chain");
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::EndChild();
    }
}

void AppUI::RenderDumperTab() {
    if (ImGui::CollapsingHeader("Structure Dumper", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputScalar("Base Address", ImGuiDataType_U64, &m_structBase, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::InputText("Struct Name", m_structName, sizeof(m_structName), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::InputInt("Instance Count", &m_structCount);
    ImGui::InputScalar("Struct Size (Manual)", ImGuiDataType_U64, &m_structSize, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);

    if (ImGui::Button("Add Field")) {
        m_structFields.push_back({"Field_" + std::to_string(m_structFields.size()), "uintptr_t", 0});
    }
    ImGui::SameLine();
    if (ImGui::Button("Discover Fields")) {
        AddLog("Attempting auto-discovery of fields near " + Utils::ToHex(m_structBase), LogSeverity::Info);

        auto modules = m_pm.GetModules();
        const size_t scanRange = 0x200; // Scan 512 bytes
        std::vector<uint8_t> buffer(scanRange);

        if (m_pm.ReadMemory(m_structBase, buffer.data(), scanRange)) {
            int discovered = 0;
            for (size_t i = 0; i < scanRange; i += sizeof(uintptr_t)) {
                uintptr_t val = *(uintptr_t*)(buffer.data() + i);

                // Check if it's a valid pointer to any module
                for (const auto& mod : modules) {
                    if (val >= mod.baseAddress && val < mod.baseAddress + mod.imageSize) {
                        bool alreadyExists = false;
                        for (const auto& f : m_structFields) if (f.offset == i) alreadyExists = true;

                        if (!alreadyExists) {
                            m_structFields.push_back({ "ptr_" + Utils::ToHex(i, false), "uintptr_t", i });
                            discovered++;
                        }
                        break;
                    }
                }
            }
            AddLog("Auto-discovery complete. Found " + std::to_string(discovered) + " potential pointers.", LogSeverity::Success);
        } else {
            AddLog("Failed to read memory for auto-discovery.", LogSeverity::Error);
        }
    }

    for (size_t i = 0; i < m_structFields.size(); ++i) {
        char nameId[32]; sprintf(nameId, "Name##%zu", i);
        char offId[32]; sprintf(offId, "Off##%zu", i);
        char typeId[32]; sprintf(typeId, "Type##%zu", i);
        char removeId[32]; sprintf(removeId, "X##%zu", i);

        ImGui::PushItemWidth(100);
        char fieldName[64];
        size_t nameLen = m_structFields[i].name.copy(fieldName, sizeof(fieldName) - 1);
        fieldName[nameLen] = '\0';
        if (ImGui::InputText(nameId, fieldName, sizeof(fieldName))) m_structFields[i].name = fieldName;
        ImGui::SameLine();
        ImGui::InputScalar(offId, ImGuiDataType_U64, &m_structFields[i].offset, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        const char* types[] = { "uintptr_t", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float", "double" };
        int currentType = 0;
        for (int k = 0; k < 11; ++k) if (m_structFields[i].type == types[k]) currentType = k;
        if (ImGui::Combo(typeId, &currentType, types, 11)) m_structFields[i].type = types[currentType];
        ImGui::SameLine();
        if (ImGui::Button(removeId)) {
            m_structFields.erase(m_structFields.begin() + i);
            i--;
        }
        ImGui::PopItemWidth();
    }

    const char* formats[] = { "JSON", "CSV", "Text" };
    ImGui::PushItemWidth(150);
    ImGui::Combo("Export Format", &m_exportFormat, formats, IM_ARRAYSIZE(formats));
    ImGui::PopItemWidth();

    ImGui::PushStyleColor(ImGuiCol_Button, m_primaryColor);
    if (ImGui::Button("Dump Structure", ImVec2(200, 35))) {
        Core::StructDefinition def = { m_structName, m_structFields, m_structSize };
        m_dumpedResults = m_dumper.DumpStructure(m_structBase, def, m_structCount);

        std::string filename = std::string(m_structName);
        bool success = false;
        if (m_exportFormat == 0) {
            filename += ".json";
            success = m_dumper.SaveToJSON(filename, m_dumpedResults);
        } else if (m_exportFormat == 1) {
            filename += ".csv";
            success = m_dumper.SaveToCSV(filename, m_dumpedResults);
        } else {
            filename += ".txt";
            success = m_dumper.SaveToText(filename, m_processName, m_pm.GetPid(), m_dumpedResults);
        }

        if (success) {
            m_status = "Dumped to " + filename;
            AddLog("Structure '" + std::string(m_structName) + "' dumped to " + formats[m_exportFormat] + ".", LogSeverity::Success);
        } else {
            m_status = "Failed to dump to " + filename;
            AddLog("Failed to dump structure to " + filename, LogSeverity::Error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load JSON", ImVec2(120, 35))) {
        if (m_dumper.LoadFromJSON(std::string(m_structName) + ".json", m_dumpedResults)) {
            m_status = "Loaded from " + std::string(m_structName) + ".json";
            AddLog("Loaded " + std::to_string(m_dumpedResults.size()) + " results from JSON.", LogSeverity::Success);
        } else {
            m_status = "Failed to load JSON";
            AddLog("Failed to load JSON: " + std::string(m_structName) + ".json", LogSeverity::Error);
        }
    }
    ImGui::PopStyleColor();
    }

    if (ImGui::CollapsingHeader("Binary Range Dump")) {
        ImGui::InputScalar("Start Address", ImGuiDataType_U64, &m_rangeStart, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::InputScalar("End Address", ImGuiDataType_U64, &m_rangeEnd, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::InputText("Data Type (e.g. float, int32)", m_rangeType, sizeof(m_rangeType));
        if (ImGui::Button("Dump Range", ImVec2(200, 35))) {
            m_dumpedResults = m_dumper.DumpRange(m_rangeStart, m_rangeEnd, m_rangeType);
            m_dumper.SaveToJSON("range_dump.json", m_dumpedResults);
            AddLog("Range dump complete.", LogSeverity::Success);
        }
    }

    if (ImGui::CollapsingHeader("Automated Data Section Analysis")) {
    static char targetModule[64] = "RainbowSix.exe";
    ImGui::InputText("Module Name##Dumper", targetModule, sizeof(targetModule), ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::PushStyleColor(ImGuiCol_Button, m_primaryColor);
    if (ImGui::Button("Analyze Data Sections", ImVec2(200, 35))) {
        AddLog("Analyzing data sections for module: " + std::string(targetModule), LogSeverity::Info);
        m_dumpedResults = m_dumper.AnalyzeDataSections(targetModule);
        m_dumper.SaveToJSON("data_analysis.json", m_dumpedResults);
        m_status = "Analysis complete. Results in data_analysis.json";
        AddLog("Data section analysis complete. Found " + std::to_string(m_dumpedResults.size()) + " potential pointers.", LogSeverity::Success);
    }
    ImGui::PopStyleColor();

    if (!m_dumpedResults.empty()) {
        ImGui::Separator();
        ImGui::Text("Results:");
        if (ImGui::BeginTable("dumpedResultsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Offset");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Description");
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_dumpedResults.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", m_dumpedResults[i].name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%llX", (unsigned long long)m_dumpedResults[i].offset);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", m_dumpedResults[i].value.c_str());
                ImGui::TableSetColumnIndex(3);
                char descBuf[1024]; // Larger buffer
                size_t descSize = m_dumpedResults[i].description.copy(descBuf, sizeof(descBuf) - 1);
                descBuf[descSize] = '\0';
                if (ImGui::InputText((std::string("##desc") + std::to_string(i)).c_str(), descBuf, sizeof(descBuf))) {
                    m_dumpedResults[i].description = descBuf;
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Save Changes to JSON")) {
            m_dumper.SaveToJSON(std::string(m_structName) + ".json", m_dumpedResults);
            AddLog("Changes saved to JSON.", LogSeverity::Success);
        }
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

void AppUI::AddToRecentProcesses(const std::string& name) {
    auto it = std::find(m_recentProcesses.begin(), m_recentProcesses.end(), name);
    if (it != m_recentProcesses.end()) m_recentProcesses.erase(it);
    m_recentProcesses.push_front(name);
    if (m_recentProcesses.size() > 5) m_recentProcesses.pop_back();
}

void AppUI::JumpToHex(uintptr_t addr) {
    m_hexBase = addr;
    AddLog("Jumping to address: " + Utils::ToHex(addr) + " in Hex Viewer", LogSeverity::Info);

    sprintf(m_hexAddrBuf, "%llX", (unsigned long long)m_hexBase);

    // Add to history
    if (m_historyIndex == -1 || m_hexHistory[m_historyIndex] != addr) {
        // Clear forward history if we are in the middle of it
        if (m_historyIndex >= 0 && m_historyIndex < (int)m_hexHistory.size() - 1) {
            m_hexHistory.erase(m_hexHistory.begin() + m_historyIndex + 1, m_hexHistory.end());
        }

        m_hexHistory.push_back(addr);
        if (m_hexHistory.size() > 50) m_hexHistory.erase(m_hexHistory.begin());
        m_historyIndex = (int)m_hexHistory.size() - 1;
    }

    m_activeTab = TabID::HexViewer;
}

void AppUI::RenderHexViewerTab() {
    // Navigation History
    ImGui::BeginGroup();
    if (ImGui::Button("<", ImVec2(30, 0)) && m_historyIndex > 0) {
        m_historyIndex--;
        m_hexBase = m_hexHistory[m_historyIndex];
        sprintf(m_hexAddrBuf, "%llX", (unsigned long long)m_hexBase);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go Back");

    ImGui::SameLine();
    if (ImGui::Button(">", ImVec2(30, 0)) && m_historyIndex < (int)m_hexHistory.size() - 1) {
        m_historyIndex++;
        m_hexBase = m_hexHistory[m_historyIndex];
        sprintf(m_hexAddrBuf, "%llX", (unsigned long long)m_hexBase);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go Forward");
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(10, 0));
    ImGui::SameLine();

    ImGui::Text("Address:");
    ImGui::SameLine();
    ImGui::PushItemWidth(150);

    // Address Validation Feedback
    uintptr_t previewAddr = 0;
    try { previewAddr = std::stoull(m_hexAddrBuf, nullptr, 16); } catch(...) {}

    uint8_t dummy;
    bool isValid = m_pm.ReadMemory(previewAddr, &dummy, 1);

    if (!isValid) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    if (ImGui::InputText("##HexAddr", m_hexAddrBuf, sizeof(m_hexAddrBuf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        try { JumpToHex(std::stoull(m_hexAddrBuf, nullptr, 16)); } catch (...) {}
    }
    if (!isValid) ImGui::PopStyleColor();
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("GO", ImVec2(50, 0))) {
        try { JumpToHex(std::stoull(m_hexAddrBuf, nullptr, 16)); } catch (...) {}
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(10, 0));
    ImGui::SameLine();

    // Module Shortcuts
    ImGui::PushItemWidth(250);
    if (ImGui::BeginCombo("##ModuleJump", "Jump to Module...", ImGuiComboFlags_HeightLarge)) {
        auto modules = m_pm.GetModules();
        for (const auto& mod : modules) {
            if (ImGui::Selectable(mod.name.c_str())) {
                JumpToHex(mod.baseAddress);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Separator();

    if (ImGui::BeginChild("HexScroll", ImVec2(0, 0), true)) {
        const int rows = 32;
        uint8_t buffer[rows * 16];
        if (m_pm.ReadMemory(m_hexBase, buffer, sizeof(buffer))) {
            for (int i = 0; i < rows; ++i) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "0x%012llX: ", (unsigned long long)(m_hexBase + i * 16));
                ImGui::SameLine();

                for (int j = 0; j < 16; ++j) {
                    uintptr_t currentByteAddr = m_hexBase + i * 16 + j;
                    uint8_t b = buffer[i * 16 + j];

                    ImGui::PushID((int)(i * 16 + j));
                    if (b == 0) ImGui::TextDisabled("00 ");
                    else if (b >= 32 && b <= 126) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%02X ", b);
                    else if (b == 0xFF) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%02X ", b);
                    else ImGui::Text("%02X ", b);

                    if (ImGui::BeginPopupContextItem("##byte_context")) {
                        static char editBuf[4] = "";
                        static uintptr_t editingAddr = 0;

                        if (editingAddr != currentByteAddr) {
                            snprintf(editBuf, sizeof(editBuf), "%02X", b);
                            editingAddr = currentByteAddr;
                        }

                        ImGui::Text("Edit Byte at 0x%llX", (unsigned long long)currentByteAddr);
                        if (ImGui::InputText("Value (Hex)", editBuf, sizeof(editBuf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                            try {
                                uint8_t newVal = (uint8_t)std::stoul(editBuf, nullptr, 16);
                                if (m_pm.WriteMemory(currentByteAddr, &newVal, 1)) {
                                    AddLog("Memory written at " + Utils::ToHex(currentByteAddr), LogSeverity::Success);
                                } else {
                                    AddLog("Failed to write memory at " + Utils::ToHex(currentByteAddr), LogSeverity::Error);
                                }
                            } catch (...) {
                                AddLog("Invalid hex value entered: " + std::string(editBuf), LogSeverity::Error);
                            }
                            ImGui::CloseCurrentPopup();
                            editingAddr = 0; // Reset for next interaction
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
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

void AppUI::AddLog(const std::string& message, LogSeverity severity) {
    std::lock_guard<std::mutex> lock(m_logMutex);

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm window_time;

#ifdef _WIN32
    localtime_s(&window_time, &in_time_t);
#else
    localtime_r(&in_time_t, &window_time);
#endif

    std::stringstream ss;
    ss << std::put_time(&window_time, "%H:%M:%S");

    m_activityLog.push_back({ message, severity, ss.str() });
    if (m_activityLog.size() > 1000) m_activityLog.pop_front();
}

void AppUI::RenderActivityLogTab() {
    if (ImGui::Button("Clear Log")) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_activityLog.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Log")) {
        std::ofstream f("activity_log.txt");
        std::lock_guard<std::mutex> lock(m_logMutex);
        for (const auto& entry : m_activityLog) {
            f << "[" << entry.timestamp << "] " << entry.message << std::endl;
        }
        AddLog("Log exported to activity_log.txt", LogSeverity::Success);
    }

    ImGui::Separator();

    if (ImGui::BeginChild("LogScroll", ImVec2(0, 0), true)) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        ImGuiListClipper clipper;
        clipper.Begin((int)m_activityLog.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const auto& entry = m_activityLog[i];

                ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
                ImGui::SameLine();

                ImVec4 color;
                switch (entry.severity) {
                    case LogSeverity::Success: color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
                    case LogSeverity::Warning: color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); break;
                    case LogSeverity::Error:   color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
                    default:                   color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break;
                }

                ImGui::TextColored(color, "%s", entry.message.c_str());
            }
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

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
