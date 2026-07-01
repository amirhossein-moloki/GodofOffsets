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

inline std::string SanitizeIdentifier(const std::string& input) {
    if (input.empty()) return "var_unknown";

    std::string result = input;

    // Replace non-alphanumeric with underscores
    for (char& c : result) {
        if (!isalnum((unsigned char)c)) {
            c = '_';
        }
    }

    // Ensure it doesn't start with a digit
    if (isdigit((unsigned char)result[0])) {
        result = "_" + result;
    }

    return result;
}

} // namespace Utils
