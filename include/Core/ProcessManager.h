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
 * @brief Core functionality for memory manipulation and process management.
 */
namespace Core {

/**
 * @brief Modes for memory access.
 */
enum class MemoryMode {
    Standard,   /**< Standard Win32 OpenProcess/ReadProcessMemory / استفاده از توابع استاندارد ویندوز */
    Stealth,    /**< Handle Elevation / Kernel-mode bypass / دور زدن لایه کاربری با استفاده از درایور */
    External    /**< Future expansion / توسعه برای متدهای خارجی */
};

/**
 * @brief Information about a PE section.
 * @details اطلاعات مربوط به سکشن‌های فایل اجرایی
 */
struct SectionInfo {
    std::string name;
    uintptr_t virtualAddress;
    size_t virtualSize;
    uint32_t characteristics;
};

/**
 * @brief Information about a loaded module.
 * @details اطلاعات مربوط به ماژول‌های بارگذاری شده در حافظه
 */
struct ModuleInfo {
    std::string name;
    std::string path;
    uintptr_t baseAddress;
    size_t imageSize;
    std::vector<SectionInfo> sections;
};

/**
 * @brief Information about a memory region.
 * @details اطلاعات مربوط به محدوده‌های حافظه
 */
struct RegionInfo {
    uintptr_t baseAddress;
    size_t size;
    DWORD protect;
    DWORD type;
    std::string moduleName;
};

/**
 * @brief Basic process information.
 * @details اطلاعات پایه یک پردازش
 */
struct ProcessInfo {
    DWORD pid;
    std::string name;
    bool is64Bit;
};

/**
 * @brief Manages interaction with a target process.
 * @details مدیریت اتصال و تعامل با پردازش هدف
 */
class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // Prevent copying
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    /**
     * @brief Retrieves a list of running processes.
     * @return A vector of ProcessInfo objects.
     * @details دریافت لیست پردازش‌های در حال اجرا
     */
    static std::vector<ProcessInfo> GetProcessList();

    /**
     * @brief Attaches to a process by its PID.
     * @param pid Process ID to attach to.
     * @param mode Memory access mode.
     * @return True if successful.
     * @details اتصال به پردازش از طریق شناسه (PID)
     */
    bool Attach(DWORD pid, MemoryMode mode = MemoryMode::Standard);

    /**
     * @brief Attaches to a process by its name.
     * @param processName Executable name.
     * @param mode Memory access mode.
     * @return True if successful.
     * @details اتصال به پردازش از طریق نام فایل اجرایی
     */
    bool Attach(const std::string& processName, MemoryMode mode = MemoryMode::Standard);

    /**
     * @brief Detaches from the current process.
     * @details قطع اتصال از پردازش فعلی
     */
    void Detach();

    /**
     * @brief Checks if connected to a process.
     * @details بررسی وضعیت اتصال به پردازش
     */
    bool IsAttached() const {
        if (m_mode == MemoryMode::Stealth) return m_hDriver.IsValid();
        return m_hProcess.IsValid();
    }

    HANDLE GetHandle() const { return m_hProcess.Get(); }
    DWORD GetPid() const { return m_pid; }
    MemoryMode GetMode() const { return m_mode; }
    bool IsTarget64Bit() const { return m_is64Bit; }

    /**
     * @brief Checks if the stealth driver is active.
     * @details بررسی بارگذاری درایور در حالت Stealth
     */
    bool IsDriverLoaded() const;

    /**
     * @brief Lists all modules in the target process.
     * @details لیست کردن تمامی ماژول‌های پردازش هدف
     */
    std::vector<ModuleInfo> GetModules() const;

    /**
     * @brief Lists all committed memory regions.
     * @details لیست کردن نواحی حافظه در دسترس
     */
    std::vector<RegionInfo> GetRegions() const;

    /**
     * @brief Gets the base address of a specific module.
     * @details دریافت آدرس پایه یک ماژول خاص
     */
    uintptr_t GetModuleBase(const std::string& moduleName) const;

    /**
     * @brief Gets detailed info about a specific module.
     * @details دریافت اطلاعات کامل یک ماژول
     */
    ModuleInfo GetModuleInfo(const std::string& moduleName) const;

    /**
     * @brief Reads memory from the target process.
     * @param address Source address.
     * @param buffer Target buffer.
     * @param size Number of bytes.
     * @param modifyProtection Temporarily change memory protection.
     * @return True if all bytes were read.
     * @details خواندن حافظه از پردازش هدف
     */
    bool ReadMemory(uintptr_t address, void* buffer, size_t size, bool modifyProtection = false) const;

    /**
     * @brief Writes memory to the target process.
     * @details نوشتن در حافظه پردازش هدف
     */
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;

    /**
     * @brief Template for reading a specific type.
     * @details قالب خواندن یک نوع داده خاص از حافظه
     */
    template<typename T>
    T Read(uintptr_t address) const {
        T buffer;
        if (ReadMemory(address, &buffer, sizeof(T)))
            return buffer;
        return T{};
    }

    /**
     * @brief Template for writing a specific type.
     * @details قالب نوشتن یک نوع داده خاص در حافظه
     */
    template<typename T>
    bool Write(uintptr_t address, const T& value) const {
        return WriteMemory(address, &value, sizeof(T));
    }

    /**
     * @brief Attempts to enable SeDebugPrivilege for the current process.
     * @details تلاش برای فعال‌سازی دسترسی خطایابی برای پردازش فعلی
     */
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
