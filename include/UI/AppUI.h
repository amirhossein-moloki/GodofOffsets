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

enum class LogLevel {
    Info,
    Success,
    Warning,
    Error
};

struct LogEntry {
    std::string message;
    LogLevel level;
    float timestamp;
};

enum class TabID {
    Process = 0,
    MemoryScanner = 1,
    SignatureScanner = 2,
    PointerScan = 3,
    StructureDumper = 4,
    HexViewer = 5,
    Logs = 6
};

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
    bool m_isAttached = false;
    std::string m_status = "Ready";
    ImVec4 m_statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

    // Process Picker
    char m_procFilter[64] = "";
    std::vector<Core::ProcessInfo> m_cachedProcesses;
    float m_lastProcRefresh = 0.0f;

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

    // Hex Viewer State
    uintptr_t m_hexBase = 0;
    char m_hexAddrBuf[32] = "0";
    TabID m_activeTab = TabID::Process;
    std::vector<uintptr_t> m_hexHistory;
    int m_historyIndex = -1;

    // UI Theme
    ImVec4 m_primaryColor = ImVec4(0.2f, 0.45f, 0.7f, 1.0f);
    ImVec4 m_accentColor = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);

    std::vector<LogEntry> m_logs;

    void RenderHeader();
    void RenderProcessTab();
    void RenderSignatureTab();
    void RenderMemoryScannerTab();
    void RenderPointerScanTab();
    void RenderDumperTab();
    void RenderHexViewerTab();
    void RenderLogsTab();

    void SetupStyles();
    void PushStatusColor(const std::string& status, bool success = true);
    void AddLog(const std::string& msg, LogLevel level = LogLevel::Info);
    void RenderProcessPicker();
    void RenderEmptyState(const char* message, const char* suggestion);
    void JumpToHex(uintptr_t addr);

    void ExportToHeader();
    Core::ScanValue GetCurrentScanValue();
};

} // namespace UI
