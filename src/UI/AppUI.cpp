#include "UI/AppUI.h"
#include <string>

namespace UI {

AppUI::AppUI(Core::ProcessManager& pm, Core::MemoryScanner& scanner, Core::OffsetDumper& dumper)
    : m_pm(pm), m_scanner(scanner), m_dumper(dumper) {
    m_processes = Core::ProcessManager::GetProcessList();
}

void AppUI::Render() {
    RenderProcessSelector();
    RenderScannerTab();
    RenderDumperTab();
}

void AppUI::RenderProcessSelector() {
    ImGui::Begin("Process Selector");
    if (ImGui::Button("Refresh")) {
        m_processes = Core::ProcessManager::GetProcessList();
    }

    if (ImGui::BeginListBox("##processes", ImVec2(-FLT_MIN, -FLT_MIN))) {
        for (int i = 0; i < (int)m_processes.size(); i++) {
            const bool is_selected = (m_selectedProcessIdx == i);
            std::string label = std::to_string(m_processes[i].pid) + " - " + m_processes[i].name;
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                m_selectedProcessIdx = i;
                m_pm.Attach(m_processes[i].pid);
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }
    ImGui::End();
}

void AppUI::RenderScannerTab() {
    ImGui::Begin("Scanner");
    if (!m_pm.IsAttached()) {
        ImGui::Text("Attach to a process first.");
        ImGui::End();
        return;
    }

    ImGui::InputText("Value", m_searchBuffer, sizeof(m_searchBuffer));

    if (ImGui::Button("First Scan")) {
        Core::ScanValue val;
        val.type = Core::DataType::Int32;
        try {
            val.value = std::stoi(m_searchBuffer);
            m_scanner.FirstScan(val, Core::ScanType::ExactValue);
        } catch (...) {}
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Scan")) {
        Core::ScanValue val;
        val.type = Core::DataType::Int32;
        try {
            val.value = std::stoi(m_searchBuffer);
            m_scanner.NextScan(val, Core::ScanType::ExactValue);
        } catch (...) {}
    }

    ImGui::Separator();
    ImGui::Text("Results: %zu", m_scanner.GetResults().size());

    if (ImGui::BeginListBox("##results", ImVec2(-FLT_MIN, -FLT_MIN))) {
        const auto& results = m_scanner.GetResults();
        for (size_t i = 0; i < (std::min)(results.size(), (size_t)1000); i++) {
            char addrStr[32];
            sprintf(addrStr, "0x%p", (void*)results[i]);
            ImGui::Selectable(addrStr);
        }
        ImGui::EndListBox();
    }

    ImGui::End();
}

void AppUI::RenderDumperTab() {
    ImGui::Begin("Dumper");
    if (ImGui::Button("Dump Base Module")) {
        // Dump logic
    }
    ImGui::End();
}

} // namespace UI
