#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#endif
#include <vector>

namespace Utils {

#ifdef _WIN32
inline std::string GetLastErrorString(DWORD errorCode = 0) {
    DWORD errorMessageID = errorCode != 0 ? errorCode : ::GetLastError();
    if (errorMessageID == 0) return std::string();

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);

    // Remove trailing newlines
    if (!message.empty() && message.back() == '\n') message.pop_back();
    if (!message.empty() && message.back() == '\r') message.pop_back();

    return message + " (" + std::to_string(errorMessageID) + ")";
}
#else
inline std::string GetLastErrorString() { return "Not implemented on this platform"; }
#endif

inline std::string ToHex(uintptr_t value, bool prefix = true, bool uppercase = true) {
    std::stringstream ss;
    if (prefix) ss << "0x";
    if (uppercase) ss << std::uppercase;
    ss << std::hex << value;
    return ss.str();
}

inline std::string ToHexPadded(uintptr_t value, int padding = 8, bool prefix = true, bool uppercase = true) {
    std::stringstream ss;
    if (prefix) ss << "0x";
    if (uppercase) ss << std::uppercase;
    ss << std::hex << std::setw(padding) << std::setfill('0') << value;
    return ss.str();
}

inline std::string ToUTF16LE(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return "";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wstrTo[0], size_needed);
    return std::string((const char*)wstrTo.data(), wstrTo.size() * sizeof(wchar_t));
#else
    return utf8; // Not implemented for non-Windows
#endif
}

} // namespace Utils
