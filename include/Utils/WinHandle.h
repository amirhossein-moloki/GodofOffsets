#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdint>
typedef void* HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
inline void CloseHandle(HANDLE) {}
#endif

namespace Utils {

class WinHandle {
public:
    explicit WinHandle(HANDLE h = nullptr) : m_h(h) {}
    ~WinHandle() { Close(); }

    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;

    WinHandle(WinHandle&& other) noexcept : m_h(other.m_h) {
        other.m_h = nullptr;
    }

    WinHandle& operator=(WinHandle&& other) noexcept {
        if (this != std::addressof(other)) {
            Close();
            m_h = other.m_h;
            other.m_h = nullptr;
        }
        return *this;
    }

    HANDLE Get() const { return m_h; }
    operator HANDLE() const { return m_h; }

    bool IsValid() const {
        return m_h != nullptr && m_h != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (IsValid()) {
            CloseHandle(m_h);
            m_h = nullptr;
        }
    }

    HANDLE* operator&() { return &m_h; }

private:
    HANDLE m_h;
};

} // namespace Utils
