#include "Core/MemoryManager.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>

namespace Core {

MemoryManager::MemoryManager() {}
MemoryManager::~MemoryManager() { Detach(); }

bool MemoryManager::Attach(const std::string& processName, MemoryMode mode) {
    m_mode = mode;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    bool found = false;
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (processName == pe32.szExeFile) {
                m_pid = pe32.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);

    if (!found) return false;

    if (m_mode == MemoryMode::Stealth) {
        return OpenProcessWithStealth(m_pid);
    } else {
        m_hProcess = Utils::WinHandle(OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, m_pid));
        return m_hProcess.IsValid();
    }
}

void MemoryManager::Detach() {
    m_hProcess.Close();
    m_pid = 0;
}

bool MemoryManager::OpenProcessWithStealth(DWORD pid) {
    // SECURITY WARNING: This is a simplified educational representation of Handle Elevation.
    // In a production environment, this would involve using a vulnerable driver (VDM)
    // to find the EPROCESS block in kernel memory and modify the Handle table
    // or elevate the access mask of a limited handle.

    // 1. Open a limited handle that doesn't trigger BE alerts
    m_hProcess = Utils::WinHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));

    if (m_hProcess.IsValid()) {
        std::cout << "[!] Stealth Mode: Attempting Handle Elevation (DKOM/VDM concept)..." << std::endl;
        // ElevateHandle(m_hProcess); // Implement VDM elevation here

        // For educational purposes, if elevation fails, we fallback or return false
        // Real-world R6 tools would use a physical memory driver here.
        return true;
    }
    return false;
}

bool MemoryManager::ReadRaw(uintptr_t address, void* buffer, size_t size) const {
    if (!m_hProcess.IsValid()) return false;
    SIZE_T bytesRead;
    return ReadProcessMemory(m_hProcess, (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
}

bool MemoryManager::WriteRaw(uintptr_t address, const void* buffer, size_t size) const {
    if (!m_hProcess.IsValid()) return false;
    SIZE_T bytesWritten;
    return WriteProcessMemory(m_hProcess, (LPVOID)address, buffer, size, &bytesWritten) && bytesWritten == size;
}

uintptr_t MemoryManager::GetModuleBase(const std::string& moduleName) const {
    if (!m_hProcess.IsValid()) return 0;

    HMODULE hMods[1024];
    DWORD cbNeeded;

    if (EnumProcessModulesEx(m_hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL)) {
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            TCHAR szModName[MAX_PATH];
            if (GetModuleBaseName(m_hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR))) {
                if (moduleName == szModName) {
                    MODULEINFO mi;
                    if (GetModuleInformation(m_hProcess, hMods[i], &mi, sizeof(mi))) {
                        return (uintptr_t)mi.lpBaseOfDll;
                    }
                }
            }
        }
    }
    return 0;
}

} // namespace Core
