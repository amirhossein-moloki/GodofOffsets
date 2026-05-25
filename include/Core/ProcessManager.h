#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <memory>
#include "Utils/WinHandle.h"

namespace Core {

struct ModuleInfo {
    std::string name;
    std::string path;
    uintptr_t baseAddress;
    size_t imageSize;
};

struct RegionInfo {
    uintptr_t baseAddress;
    size_t size;
    DWORD protect;
    DWORD type;
    std::string moduleName;
};

struct ProcessInfo {
    DWORD pid;
    std::string name;
    bool is64Bit;
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    static std::vector<ProcessInfo> GetProcessList();
    bool Attach(DWORD pid);
    void Detach();

    bool IsAttached() const { return m_hProcess.IsValid(); }
    HANDLE GetHandle() const { return m_hProcess.Get(); }
    DWORD GetPid() const { return m_pid; }

    std::vector<ModuleInfo> GetModules() const;
    std::vector<RegionInfo> GetRegions() const;

    bool ReadMemory(uintptr_t address, void* buffer, size_t size) const;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;

    static bool EnableDebugPrivilege();

private:
    Utils::WinHandle m_hProcess;
    DWORD m_pid;
};

} // namespace Core
