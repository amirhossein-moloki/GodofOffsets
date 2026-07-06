#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include "Core/ProcessManager.h"
#include "Core/Scanner.h"
#include "Core/MemoryScanner.h"
#include "Core/PointerScanner.h"
#include "Core/OffsetDumper.h"
#include "imgui.h"

namespace UI {

enum class TabID {
    None = -1,
    Process = 0,
    MemoryScanner,
    SignatureScanner,
    PointerScanner,
    Dumper,
    HexViewer,
    ActivityLog
};

enum class LogSeverity {
    Info,
    Success,
    Warning,
    Error
};

struct LogEntry {
    std::string message;
    LogSeverity severity;
    std::string timestamp;
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

    char m_processName[64] = "";
    bool m_isAttached = false;
    std::string m_status = "Ready";
    ImVec4 m_statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

    // Window Visibility
    bool m_showProcessWindow = true;
    bool m_showScannerWindow = true;
    bool m_showSignatureWindow = true;
    bool m_showPointerWindow = true;
    bool m_showDumperWindow = true;
    bool m_showHexWindow = true;
    bool m_showActivityLogWindow = true;

    // Process Picker
    char m_procFilter[64] = "";
    std::vector<Core::ProcessInfo> m_cachedProcesses;
    std::deque<std::string> m_recentProcesses;
    float m_lastProcRefresh = 0.0f;

    // Memory Scanner UI state
    Core::DataType m_selectedDataType = Core::DataType::Int32;
    Core::ScanType m_selectedScanType = Core::ScanType::ExactValue;
    char m_scanValueBuf[128] = "0";
    char m_scanValueBuf2[128] = "0";
    bool m_modifyProtection = false;

    // Pointer Scan UI
    uintptr_t m_ptrTarget = 0;
    int m_ptrDepth = 3;
    int m_ptrOffset = 1024;
    std::vector<Core::PointerChain> m_ptrResults;
    std::map<std::string, std::map<uintptr_t, std::vector<Core::PointerChain>>> m_groupedPtrResults;

    // Structure Dump UI
    uintptr_t m_structBase = 0;
    char m_structName[64] = "MyStruct";
    int m_structCount = 1;
    size_t m_structSize = 0;
    std::vector<Core::StructField> m_structFields;
    std::vector<Core::OffsetResult> m_dumpedResults;

    // Range Dump UI
    uintptr_t m_rangeStart = 0;
    uintptr_t m_rangeEnd = 0;
    char m_rangeType[32] = "uint32";

    // Hex Viewer State
    uintptr_t m_hexBase = 0;
    char m_hexAddrBuf[32] = "0";
    TabID m_activeTab = TabID::Process;
    std::vector<uintptr_t> m_hexHistory;
    int m_historyIndex = -1;

    bool m_showDisclaimer = true;

    // Activity Log
    std::deque<LogEntry> m_activityLog;
    std::mutex m_logMutex;

    // UI Theme
    ImVec4 m_primaryColor = ImVec4(0.2f, 0.45f, 0.7f, 1.0f);
    ImVec4 m_accentColor = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);

    void RenderHeader();
    void RenderProcessTab();
    void RenderSignatureTab();
    void RenderMemoryScannerTab();
    void RenderPointerScanTab();
    void RenderDumperTab();
    void RenderHexViewerTab();
    void RenderActivityLogTab();

    void SetupStyles();
    void AddLog(const std::string& message, LogSeverity severity = LogSeverity::Info);
    void PushStatusColor(const std::string& status, bool success = true);
    void RenderProcessPicker();
    void RenderEmptyState(const char* message, const char* suggestion);
    void JumpToHex(uintptr_t addr);
    void AddToRecentProcesses(const std::string& name);

    void ExportToHeader();
    Core::ScanValue GetCurrentScanValue();
};

} // namespace UI
