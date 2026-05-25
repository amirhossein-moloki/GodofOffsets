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

class MemoryManager {
public:
    MemoryManager();
    ~MemoryManager();

    // Prevent copying
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    bool Attach(const std::string& processName, MemoryMode mode = MemoryMode::Standard);
    void Detach();

    bool ReadRaw(uintptr_t address, void* buffer, size_t size) const;
    bool WriteRaw(uintptr_t address, const void* buffer, size_t size) const;

    template<typename T>
    T Read(uintptr_t address) const {
        T buffer;
        if (ReadRaw(address, &buffer, sizeof(T)))
            return buffer;
        return T{};
    }

    template<typename T>
    bool Write(uintptr_t address, const T& value) const {
        return WriteRaw(address, &value, sizeof(T));
    }

    bool IsValid() const { return m_hProcess.IsValid(); }
    DWORD GetPid() const { return m_pid; }
    uintptr_t GetModuleBase(const std::string& moduleName) const;

private:
    Utils::WinHandle m_hProcess;
    DWORD m_pid = 0;
    MemoryMode m_mode = MemoryMode::Standard;

    bool OpenProcessWithStealth(DWORD pid);
    // VDM-related logic would go here (simplified for educational purposes)
    bool ElevateHandle(HANDLE hProcess);
};

} // namespace Core
