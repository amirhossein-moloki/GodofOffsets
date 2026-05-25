#include "Core/ProcessManager.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>

namespace Core {

ProcessManager::ProcessManager() : m_pid(0) {}

ProcessManager::~ProcessManager() {}

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

bool ProcessManager::Attach(DWORD pid) {
    Detach();
    m_hProcess = Utils::WinHandle(OpenProcess(PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid));
    if (m_hProcess.IsValid()) {
        m_pid = pid;
        return true;
    }
    return false;
}

void ProcessManager::Detach() {
    m_hProcess.Close();
    m_pid = 0;
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
                modules.push_back({ szModName, szModPath, (uintptr_t)mi.lpBaseOfDll, (size_t)mi.SizeOfImage });
            }
        }
    }

    return modules;
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
        if (nextAddress <= address) break; // Overflow check
        address = nextAddress;
    }

    return regions;
}

bool ProcessManager::ReadMemory(uintptr_t address, void* buffer, size_t size) const {
    SIZE_T bytesRead;
    return ReadProcessMemory(m_hProcess, (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
}

bool ProcessManager::WriteMemory(uintptr_t address, const void* buffer, size_t size) const {
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
    return result && GetLastError() == ERROR_SUCCESS;
}

} // namespace Core
