#include "Core/ProcessManager.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <algorithm>

namespace Core {

ProcessManager::ProcessManager() : m_pid(0), m_mode(MemoryMode::Standard) {}

ProcessManager::~ProcessManager() { Detach(); }

std::vector<ProcessInfo> ProcessManager::GetProcessList() {
    std::vector<ProcessInfo> processes;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return processes;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe32)) {
        do {
            bool is64Bit = true;
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProc) {
                BOOL wow64 = FALSE;
                if (IsWow64Process(hProc, &wow64)) {
                    is64Bit = !wow64;
                }
                CloseHandle(hProc);
            }
            processes.push_back({ pe32.th32ProcessID, pe32.szExeFile, is64Bit });
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return processes;
}

bool ProcessManager::Attach(DWORD pid, MemoryMode mode) {
    Detach();
    m_mode = mode;
    m_pid = pid;

    if (m_mode == MemoryMode::Stealth) {
        return OpenProcessWithStealth(m_pid);
    } else {
        m_hProcess = Utils::WinHandle(OpenProcess(PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, m_pid));
        return m_hProcess.IsValid();
    }
}

bool ProcessManager::Attach(const std::string& processName, MemoryMode mode) {
    auto processes = GetProcessList();
    for (const auto& proc : processes) {
        if (proc.name == processName) {
            return Attach(proc.pid, mode);
        }
    }
    return false;
}

void ProcessManager::Detach() {
    m_hProcess.Close();
    m_pid = 0;
    m_mode = MemoryMode::Standard;
}

bool ProcessManager::OpenProcessWithStealth(DWORD pid) {
    m_hProcess = Utils::WinHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (m_hProcess.IsValid()) {
        std::cout << "[!] Stealth Mode: Attached" << std::endl;
        return true;
    }
    return false;
}

bool ProcessManager::ElevateHandle(HANDLE hProcess) {
    return false;
}

std::vector<ModuleInfo> ProcessManager::GetModules() const {
    std::vector<ModuleInfo> modules;
    if (!m_hProcess) return modules;

    HMODULE hMods[1024];
    DWORD cbNeeded;

    if (EnumProcessModulesEx(m_hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL)) {
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            TCHAR szModName[MAX_PATH];
            TCHAR szModPath[MAX_PATH];
            MODULEINFO mi;

            if (GetModuleFileNameEx(m_hProcess, hMods[i], szModPath, sizeof(szModPath) / sizeof(TCHAR)) &&
                GetModuleBaseName(m_hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)) &&
                GetModuleInformation(m_hProcess, hMods[i], &mi, sizeof(mi))) {

                ModuleInfo mod;
                mod.name = szModName;
                mod.path = szModPath;
                mod.baseAddress = (uintptr_t)mi.lpBaseOfDll;
                mod.imageSize = (size_t)mi.SizeOfImage;
                mod.sections = ParseSections(mod.baseAddress);

                modules.push_back(mod);
            }
        }
    }

    return modules;
}

std::vector<SectionInfo> ProcessManager::ParseSections(uintptr_t baseAddress) const {
    std::vector<SectionInfo> sections;

    IMAGE_DOS_HEADER dosHeader = Read<IMAGE_DOS_HEADER>(baseAddress);
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) return sections;

    // Read NT Headers to determine architecture
    uintptr_t ntHeaderAddr = baseAddress + dosHeader.e_lfanew;
    IMAGE_NT_HEADERS64 ntHeaders = Read<IMAGE_NT_HEADERS64>(ntHeaderAddr);
    if (ntHeaders.Signature != IMAGE_NT_SIGNATURE) return sections;

    uintptr_t sectionHeaderAddr = 0;
    int numSections = 0;

    if (ntHeaders.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        // 64-bit
        sectionHeaderAddr = ntHeaderAddr + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + ntHeaders.FileHeader.SizeOfOptionalHeader;
        numSections = ntHeaders.FileHeader.NumberOfSections;
    } else {
        // 32-bit
        IMAGE_NT_HEADERS32 ntHeaders32 = Read<IMAGE_NT_HEADERS32>(ntHeaderAddr);
        sectionHeaderAddr = ntHeaderAddr + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + ntHeaders32.FileHeader.SizeOfOptionalHeader;
        numSections = ntHeaders32.FileHeader.NumberOfSections;
    }

    for (int i = 0; i < numSections; ++i) {
        IMAGE_SECTION_HEADER sectionHeader = Read<IMAGE_SECTION_HEADER>(sectionHeaderAddr + (i * sizeof(IMAGE_SECTION_HEADER)));

        SectionInfo section;
        char name[9] = {0};
        memcpy(name, sectionHeader.Name, 8);
        section.name = name;
        section.virtualAddress = baseAddress + sectionHeader.VirtualAddress;
        section.virtualSize = sectionHeader.Misc.VirtualSize;
        section.characteristics = sectionHeader.Characteristics;

        sections.push_back(section);
    }

    return sections;
}

std::vector<RegionInfo> ProcessManager::GetRegions() const {
    std::vector<RegionInfo> regions;
    if (!m_hProcess) return regions;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0;

    auto modules = GetModules();

    while (VirtualQueryEx(m_hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD)) {
            std::string moduleName = "";
            for (const auto& mod : modules) {
                if (address >= mod.baseAddress && address < mod.baseAddress + mod.imageSize) {
                    moduleName = mod.name;
                    break;
                }
            }
            regions.push_back({ (uintptr_t)mbi.BaseAddress, mbi.RegionSize, mbi.Protect, mbi.Type, moduleName });
        }

        uintptr_t nextAddress = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (nextAddress <= address) break;
        address = nextAddress;
    }

    return regions;
}

uintptr_t ProcessManager::GetModuleBase(const std::string& moduleName) const {
    auto modules = GetModules();
    for (const auto& mod : modules) {
        if (mod.name == moduleName) {
            return mod.baseAddress;
        }
    }
    return 0;
}

ModuleInfo ProcessManager::GetModuleInfo(const std::string& moduleName) const {
    auto modules = GetModules();
    for (const auto& mod : modules) {
        if (mod.name == moduleName) {
            return mod;
        }
    }
    return {};
}

bool ProcessManager::ReadMemory(uintptr_t address, void* buffer, size_t size) const {
    if (!m_hProcess.IsValid()) return false;
    SIZE_T bytesRead;
    return ReadProcessMemory(m_hProcess, (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
}

bool ProcessManager::WriteMemory(uintptr_t address, const void* buffer, size_t size) const {
    if (!m_hProcess.IsValid()) return false;
    SIZE_T bytesWritten;
    return WriteProcessMemory(m_hProcess, (LPVOID)address, buffer, size, &bytesWritten) && bytesWritten == size;
}

bool ProcessManager::EnableDebugPrivilege() {
    HANDLE hToken;
    LUID luid;
    TOKEN_PRIVILEGES tp;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    bool result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
    CloseHandle(hToken);
    return result && (GetLastError() == ERROR_SUCCESS);
}

} // namespace Core
