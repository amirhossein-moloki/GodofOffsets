#pragma once

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#else
#include <cstdint>
typedef uint32_t DWORD;
typedef uint64_t UINT64;

#ifndef CTL_CODE
#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)
#endif

#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#endif

// Define the device type
#define DUMPER_DEVICE_TYPE 0x00008001

// Define IOCTL codes
#define IOCTL_DUMPER_GET_PID           CTL_CODE(DUMPER_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DUMPER_GET_MODULE_BASE   CTL_CODE(DUMPER_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DUMPER_READ_MEMORY       CTL_CODE(DUMPER_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DUMPER_HIDE_DRIVER       CTL_CODE(DUMPER_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Structure for PID request
typedef struct _DUMPER_GET_PID_REQUEST {
    char process_name[260];
    DWORD pid;
} DUMPER_GET_PID_REQUEST, *PDUMPER_GET_PID_REQUEST;

// Structure for Module Base request
typedef struct _DUMPER_GET_MODULE_REQUEST {
    DWORD pid;
    char module_name[260];
    UINT64 base_address;
} DUMPER_GET_MODULE_REQUEST, *PDUMPER_GET_MODULE_REQUEST;

// Structure for Memory Read request
typedef struct _DUMPER_READ_MEMORY_REQUEST {
    DWORD pid;
    UINT64 address;
    UINT64 buffer;
    UINT64 size;
} DUMPER_READ_MEMORY_REQUEST, *PDUMPER_READ_MEMORY_REQUEST;
