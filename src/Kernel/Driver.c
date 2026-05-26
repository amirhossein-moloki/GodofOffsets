#include "Driver.h"
#include "../../include/Shared/Ioctl.h"

// Device and Symbolic Link names
UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KernelDumper");
UNICODE_STRING SymbolicLink = RTL_CONSTANT_STRING(L"\\DosDevices\\KernelDumper");

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    PDEVICE_OBJECT DeviceObject = NULL;

    status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = IoCreateSymbolicLink(&SymbolicLink, &DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(DeviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DumperCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DumperCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DumperDeviceControl;
    DriverObject->DriverUnload = DriverUnload;

    DeviceObject->Flags |= DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[KernelDumper] Driver Loaded Successfully\n");

    return STATUS_SUCCESS;
}

void DriverUnload(PDRIVER_OBJECT DriverObject) {
    IoDeleteSymbolicLink(&SymbolicLink);
    IoDeleteDevice(DriverObject->DeviceObject);
    DbgPrint("[KernelDumper] Driver Unloaded\n");
}

NTSTATUS DumperCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DumperDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (!stack) return STATUS_INVALID_PARAMETER;

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG bytesTransferred = 0;

    ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inputLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;

    switch (controlCode) {
        case IOCTL_DUMPER_GET_PID: {
            if (inputLength >= sizeof(DUMPER_GET_PID_REQUEST)) {
                PDUMPER_GET_PID_REQUEST request = (PDUMPER_GET_PID_REQUEST)buffer;
                status = GetProcessPidByName(request->process_name, &request->pid);
                bytesTransferred = sizeof(DUMPER_GET_PID_REQUEST);
            }
            break;
        }

        case IOCTL_DUMPER_GET_MODULE_BASE: {
            if (inputLength >= sizeof(DUMPER_GET_MODULE_REQUEST)) {
                PDUMPER_GET_MODULE_REQUEST request = (PDUMPER_GET_MODULE_REQUEST)buffer;
                // Convert char to wchar for module name comparison if needed
                // For simplicity, assuming ASCII for now or specialized helper
                WCHAR wModuleName[260];
                ANSI_STRING ansiStr;
                UNICODE_STRING uniStr;
                RtlInitAnsiString(&ansiStr, request->module_name);
                uniStr.Buffer = wModuleName;
                uniStr.MaximumLength = sizeof(wModuleName);
                RtlAnsiStringToUnicodeString(&uniStr, &ansiStr, FALSE);

                status = GetModuleBaseAddress(request->pid, wModuleName, &request->base_address);
                bytesTransferred = sizeof(DUMPER_GET_MODULE_REQUEST);
            }
            break;
        }

        case IOCTL_DUMPER_READ_MEMORY: {
            if (inputLength >= sizeof(DUMPER_READ_MEMORY_REQUEST)) {
                PDUMPER_READ_MEMORY_REQUEST request = (PDUMPER_READ_MEMORY_REQUEST)buffer;
                status = CopyVirtualMemory(request->pid, (PVOID)request->address, (PVOID)request->buffer, (SIZE_T)request->size);
                bytesTransferred = sizeof(DUMPER_READ_MEMORY_REQUEST);
            }
            break;
        }

        case IOCTL_DUMPER_HIDE_DRIVER: {
            status = HideDriver(DeviceObject->DriverObject);
            break;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesTransferred;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

// Helper Implementation
NTSTATUS GetProcessPidByName(PCSTR processName, PDWORD pid) {
    PEPROCESS process = NULL;
    NTSTATUS status;

    // Improved PID discovery logic using safe iteration and name matching
    for (ULONG i = 4; i < 100000; i += 4) {
        status = PsLookupProcessByProcessId((HANDLE)i, &process);
        if (NT_SUCCESS(status)) {
            __try {
                PCSTR currentName = (PCSTR)PsGetProcessImageFileName(process);
                if (currentName && strstr(currentName, processName)) {
                    *pid = (DWORD)i;
                    ObDereferenceObject(process);
                    return STATUS_SUCCESS;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                // Skip on access violation
            }
            ObDereferenceObject(process);
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS GetModuleBaseAddress(DWORD pid, PCWSTR moduleName, PUINT64 baseAddress) {
    PEPROCESS process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)pid, &process);
    if (!NT_SUCCESS(status)) return status;

    KAPC_STATE apc;
    KeStackAttachProcess(process, &apc);

    __try {
        PPEB peb = PsGetProcessPeb(process);
        if (peb) {
            PPEB_LDR_DATA ldr = peb->Ldr;
            if (ldr) {
                UNICODE_STRING targetName;
                RtlInitUnicodeString(&targetName, moduleName);

                for (PLIST_ENTRY entry = ldr->InLoadOrderModuleList.Flink; entry != &ldr->InLoadOrderModuleList; entry = entry->Flink) {
                    PLDR_DATA_TABLE_ENTRY module = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                    if (RtlCompareUnicodeString(&module->BaseDllName, &targetName, TRUE) == 0) {
                        *baseAddress = (UINT64)module->DllBase;
                        status = STATUS_SUCCESS;
                        break;
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = STATUS_ACCESS_VIOLATION;
    }

    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS HideDriver(PDRIVER_OBJECT DriverObject) {
    PLDR_DATA_TABLE_ENTRY ldr = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;
    if (!ldr) return STATUS_UNSUCCESSFUL;

    // Unlink from InLoadOrderLinks
    ldr->InLoadOrderLinks.Flink->Blink = ldr->InLoadOrderLinks.Blink;
    ldr->InLoadOrderLinks.Blink->Flink = ldr->InLoadOrderLinks.Flink;

    // Zero out the links
    ldr->InLoadOrderLinks.Flink = &ldr->InLoadOrderLinks;
    ldr->InLoadOrderLinks.Blink = &ldr->InLoadOrderLinks;

    return STATUS_SUCCESS;
}

NTSTATUS CopyVirtualMemory(DWORD pid, PVOID sourceAddress, PVOID targetAddress, SIZE_T size) {
    PEPROCESS process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)pid, &process);
    if (NT_SUCCESS(status)) {
        SIZE_T bytesCopied = 0;
        __try {
            status = MmCopyVirtualMemory(process, sourceAddress, PsGetCurrentProcess(), targetAddress, size, KernelMode, &bytesCopied);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            status = STATUS_ACCESS_VIOLATION;
        }
        ObDereferenceObject(process);
    }
    return status;
}
