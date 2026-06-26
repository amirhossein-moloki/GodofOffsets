#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Utils {

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

inline std::string GetLastErrorAsString(DWORD errorCode) {
#ifdef _WIN32
    if (errorCode == 0) return "Success";

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);

    // Remove trailing newlines
    message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
    message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());

    return message;
#else
    return "Error " + std::to_string(errorCode);
#endif
}

} // namespace Utils
