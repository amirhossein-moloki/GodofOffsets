#pragma once
#include <vector>
#include <string>
#include "Core/ProcessManager.h"
#include "Core/Scanner.h"
#include "Core/MemoryScanner.h"
#include "Core/PointerScanner.h"
#include "Core/OffsetDumper.h"
#include "imgui.h"

namespace UI {

class AppUI {
public:
    AppUI(Core::ProcessManager& pm, Core::Scanner& scanner);
    void Render();

private:
    Core::ProcessManager& m_pm;
    Core::Scanner& m_scanner;
    Core::MemoryScanner m_memScanner;
    Core::PointerScanner m_ptrScanner;
    Core::OffsetDumper m_dumper;

    std::vector<Core::Signature> m_sigs;

    char m_processName[64] = "RainbowSix.exe";
    char m_processFilter[64] = "";
    bool m_isAttached = false;
    std::string m_status = "Ready";

    // Memory Scanner UI state
    Core::DataType m_selectedDataType = Core::DataType::Int32;
    Core::ScanType m_selectedScanType = Core::ScanType::ExactValue;
    char m_scanValueBuf[128] = "0";
    char m_scanValueBuf2[128] = "0";

    // Pointer Scan UI
    uintptr_t m_ptrTarget = 0;
    int m_ptrDepth = 3;
    int m_ptrOffset = 1024;
    std::vector<Core::PointerChain> m_ptrResults;

    // Structure Dump UI
    uintptr_t m_structBase = 0;
    char m_structName[64] = "MyStruct";
    std::vector<Core::StructField> m_structFields;

    void RenderHeader();
    void RenderProcessTab();
    void RenderSignatureTab();
    void RenderMemoryScannerTab();
    void RenderPointerScanTab();
    void RenderDumperTab();
    void RenderHexViewerTab();

    void ExportToHeader();
    Core::ScanValue GetCurrentScanValue();
};

} // namespace UI
