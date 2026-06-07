#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdint>
#include <cstring>
typedef uint32_t DWORD;
typedef void* HANDLE;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t UINT32;
#define FALSE 0
#define TRUE 1
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

typedef struct _MEMORY_BASIC_INFORMATION {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    size_t RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 0x01
#define PAGE_GUARD 0x100

#endif

#include <vector>
#include <string>
#include <memory>
#include "Utils/WinHandle.h"

/**
 * @namespace Core
 * @brief contains core logic for process and memory management.
 * @details شامل منطق اصلی مدیریت پروسس‌ها و حافظه.
 */
namespace Core {

/**
 * @enum MemoryMode
 * @brief Defines the level of stealth used for memory operations.
 * @details متدهای مختلف دسترسی به حافظه (استاندارد، مخفی و خارجی).
 */
enum class MemoryMode {
    Standard,   // Standard OpenProcess/ReadProcessMemory (متد استاندارد ویندوز)
    Stealth,    // Handle Elevation / VDM (دور زدن آنتی‌چیت با درایور)
    External    // For future expansion (e.g., KVM, DMA) (توسعه آتی برای سخت‌افزار)
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

/**
 * @class ProcessManager
 * @brief Manages process attachment and memory access.
 * @details کلاس مدیریت اتصال به پروسس و خواندن/نوشتن در حافظه.
 */
class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // Prevent copying
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    /**
     * @brief Retrieves a list of all active processes.
     * @return std::vector<ProcessInfo> لیست پروسس‌های فعال.
     */
    static std::vector<ProcessInfo> GetProcessList();

    /**
     * @brief Attaches to a process by its PID.
     * @param pid Process ID.
     * @param mode Selected memory access mode.
     * @return true if successful.
     */
    bool Attach(DWORD pid, MemoryMode mode = MemoryMode::Standard);

    /**
     * @brief Attaches to a process by its executable name.
     * @param processName Name of the .exe file.
     * @param mode Selected memory access mode.
     * @return true if successful.
     */
    bool Attach(const std::string& processName, MemoryMode mode = MemoryMode::Standard);

    /**
     * @brief Detaches from the current target process.
     * @details قطع اتصال از پروسس هدف.
     */
    void Detach();

    bool IsAttached() const {
        if (m_mode == MemoryMode::Stealth) return m_hDriver.IsValid();
        return m_hProcess.IsValid();
    }
    HANDLE GetHandle() const { return m_hProcess.Get(); }
    DWORD GetPid() const { return m_pid; }
    MemoryMode GetMode() const { return m_mode; }
    bool IsTarget64Bit() const { return m_is64Bit; }

    bool IsDriverLoaded() const;

    std::vector<ModuleInfo> GetModules() const;
    std::vector<RegionInfo> GetRegions() const;
    uintptr_t GetModuleBase(const std::string& moduleName) const;
    ModuleInfo GetModuleInfo(const std::string& moduleName) const;

    /**
     * @brief Reads memory from the target process.
     * @param address Target virtual address.
     * @param buffer Buffer to store the data.
     * @param size Number of bytes to read.
     * @param modifyProtection If true, temporarily changes memory protection to PAGE_EXECUTE_READWRITE.
     * @return true if read was successful.
     */
    bool ReadMemory(uintptr_t address, void* buffer, size_t size, bool modifyProtection = false) const;

    /**
     * @brief Writes memory to the target process.
     * @param address Target virtual address.
     * @param buffer Buffer containing the data to write.
     * @param size Number of bytes to write.
     * @return true if write was successful.
     */
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;

    /**
     * @brief Helper template to read a specific type from memory.
     * @tparam T Type to read (e.g., int, float).
     * @param address Virtual address to read from.
     * @return The value read from memory.
     */
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
    Utils::WinHandle m_hDriver;
    DWORD m_pid;
    bool m_is64Bit = true;
    MemoryMode m_mode = MemoryMode::Standard;

    bool OpenProcessWithStealth(DWORD pid);
    bool ElevateHandle(HANDLE hProcess);

    std::vector<SectionInfo> ParseSections(uintptr_t baseAddress) const;
};

} // namespace Core
