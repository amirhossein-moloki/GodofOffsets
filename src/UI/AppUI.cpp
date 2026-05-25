#include "UI/AppUI.h"
#include <fstream>
#include <iomanip>

namespace UI {

AppUI::AppUI(Core::MemoryManager& mm, Core::Scanner& scanner)
    : m_mm(mm), m_scanner(scanner) {
    m_sigs = m_scanner.LoadSignatures("signatures.json");
}

void AppUI::Render() {
    ImGui::Begin("R6 Professional Offset Dumper");

    RenderHeader();
    ImGui::Separator();
    RenderControls();
    ImGui::Separator();
    RenderResultsTable();

    ImGui::End();
}

void AppUI::RenderHeader() {
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "SECURITY WARNING: RESEARCH USE ONLY");
    ImGui::Text("Status: %s", m_status.c_str());
}

void AppUI::RenderControls() {
    ImGui::InputText("Process Name", m_processName, sizeof(m_processName));

    if (ImGui::Button("Attach (Standard)")) {
        if (m_mm.Attach(m_processName, Core::MemoryMode::Standard)) {
            m_isAttached = true;
            m_status = "Attached (Standard)";
        } else {
            m_status = "Failed to Attach";
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Attach (Stealth/VDM)")) {
        if (m_mm.Attach(m_processName, Core::MemoryMode::Stealth)) {
            m_isAttached = true;
            m_status = "Attached (Stealth Mode)";
        } else {
            m_status = "Failed to Attach Stealth";
        }
    }

    if (m_isAttached) {
        if (ImGui::Button("Run Scanner")) {
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
    }
}

void AppUI::RenderResultsTable() {
    if (ImGui::BeginTable("results", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
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
                ImGui::Text("0x%llX", sig.result);

                ImGui::TableSetColumnIndex(2);
                uintptr_t base = m_mm.GetModuleBase(sig.moduleName);
                ImGui::Text("0x%llX", sig.result - base);
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Not Found");
            }
        }
        ImGui::EndTable();
    }
}

void AppUI::ExportToHeader() {
    std::ofstream f("offsets.h");
    f << "#pragma once\n\n";
    f << "namespace Offsets {\n";
    for (const auto& sig : m_sigs) {
        if (sig.result) {
            uintptr_t base = m_mm.GetModuleBase(sig.moduleName);
            f << "    constexpr unsigned long long " << sig.name << " = 0x"
              << std::hex << std::uppercase << (sig.result - base) << ";\n";
        }
    }
    f << "}\n";
}

} // namespace UI
