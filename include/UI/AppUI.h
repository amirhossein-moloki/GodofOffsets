#pragma once
#include <vector>
#include <string>
#include "Core/MemoryManager.h"
#include "Core/Scanner.h"
#include "imgui.h"

namespace UI {

class AppUI {
public:
    AppUI(Core::MemoryManager& mm, Core::Scanner& scanner);
    void Render();

private:
    Core::MemoryManager& m_mm;
    Core::Scanner& m_scanner;
    std::vector<Core::Signature> m_sigs;

    char m_processName[64] = "RainbowSix.exe";
    bool m_isAttached = false;
    std::string m_status = "Ready";

    void RenderHeader();
    void RenderControls();
    void RenderResultsTable();
    void ExportToHeader();
};

} // namespace UI
