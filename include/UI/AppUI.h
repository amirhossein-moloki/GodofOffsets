#pragma once
#include "imgui.h"
#include "Core/ProcessManager.h"
#include "Core/MemoryScanner.h"
#include "Core/OffsetDumper.h"
#include <vector>

namespace UI {

class AppUI {
public:
    AppUI(Core::ProcessManager& pm, Core::MemoryScanner& scanner, Core::OffsetDumper& dumper);
    void Render();

private:
    Core::ProcessManager& m_pm;
    Core::MemoryScanner& m_scanner;
    Core::OffsetDumper& m_dumper;

    void RenderProcessSelector();
    void RenderScannerTab();
    void RenderDumperTab();

    std::vector<Core::ProcessInfo> m_processes;
    int m_selectedProcessIdx = -1;

    char m_searchBuffer[256] = "";
};

} // namespace UI
