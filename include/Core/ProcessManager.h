#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <memory>
#include "Utils/WinHandle.h"

namespace Core {

enum class MemoryMode {
    Standard,   // Standard OpenProcess/ReadProcessMemory
    Stealth,    // Handle Elevation / VDM (Kernel-mode bypass)
    External    // For future expansion (e.g., KVM, DMA)
};

struct SectionInfo {
    std::string name;
    uintptr_t virtualAddress;
    size_t virtualSize;
    uint32_t characteristics;
};

struct ModuleInfo {
    std::string name;
    std::string path;
    uintptr_t baseAddress;
    size_t imageSize;
    std::vector<SectionInfo> sections;
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

    // Prevent copying
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    static std::vector<ProcessInfo> GetProcessList();
    bool Attach(DWORD pid, MemoryMode mode = MemoryMode::Standard);
    bool Attach(const std::string& processName, MemoryMode mode = MemoryMode::Standard);
    void Detach();

    bool IsAttached() const { return m_hProcess.IsValid(); }
    HANDLE GetHandle() const { return m_hProcess.Get(); }
    DWORD GetPid() const { return m_pid; }
    MemoryMode GetMode() const { return m_mode; }

    std::vector<ModuleInfo> GetModules() const;
    std::vector<RegionInfo> GetRegions() const;
    uintptr_t GetModuleBase(const std::string& moduleName) const;
    ModuleInfo GetModuleInfo(const std::string& moduleName) const;

    bool ReadMemory(uintptr_t address, void* buffer, size_t size) const;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;

    template<typename T>
    T Read(uintptr_t address) const {
        T buffer;
        if (ReadMemory(address, &buffer, sizeof(T)))
            return buffer;
        return T{};
    }

    template<typename T>
    bool Write(uintptr_t address, const T& value) const {
        return WriteMemory(address, &value, sizeof(T));
    }

    static bool EnableDebugPrivilege();

private:
    Utils::WinHandle m_hProcess;
    DWORD m_pid;
    MemoryMode m_mode = MemoryMode::Standard;

    bool OpenProcessWithStealth(DWORD pid);
    bool ElevateHandle(HANDLE hProcess);

    std::vector<SectionInfo> ParseSections(uintptr_t baseAddress) const;
};

} // namespace Core
