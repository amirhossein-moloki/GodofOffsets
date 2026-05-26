#pragma once
#include <ntifs.h>
#include <ntddk.h>

// Undocumented functions and structures
NTKERNELAPI PCHAR PsGetProcessImageFileName(PEPROCESS Process);
NTKERNELAPI PPEB PsGetProcessPeb(PEPROCESS Process);

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB_LDR_DATA {
    ULONG Length;
    UCHAR Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _PEB {
    UCHAR InheritedAddressSpace;
    UCHAR ReadImageFileExecOptions;
    UCHAR BeingDebugged;
    UCHAR BitField;
    PVOID Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
} PEB, *PPEB;

// Forward declarations
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
NTSTATUS DumperCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DumperDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
void DriverUnload(PDRIVER_OBJECT DriverObject);

// Helper functions
NTSTATUS GetProcessPidByName(PCSTR processName, PDWORD pid);
NTSTATUS GetModuleBaseAddress(DWORD pid, PCWSTR moduleName, PUINT64 baseAddress);
NTSTATUS CopyVirtualMemory(DWORD pid, PVOID sourceAddress, PVOID targetAddress, SIZE_T size);
NTSTATUS HideDriver(PDRIVER_OBJECT DriverObject);
