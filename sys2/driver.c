#include <ntifs.h>
#include <ntstrsafe.h>
#include <ntddk.h>
#include <string.h>

#include <sys\driver.h>
#include "internal.h"


NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING ntUnicodeString = RTL_CONSTANT_STRING(NT_DEVICE_NAME);

    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS ntStatus = IoCreateDevice(
        DriverObject,             // Our Driver Object
        0,                        // We don't use a device extension
        &ntUnicodeString,         // Device name "\Device\DPROCMON2"
        FILE_DEVICE_UNKNOWN,      // Device type
        FILE_DEVICE_SECURE_OPEN,  // Device characteristics
        FALSE,                    // Not an exclusive device
        &deviceObject             // Returned ptr to Device Object
    );

    if (!NT_SUCCESS(ntStatus)) {
        DPROCMON_KDPRINT("Couldn't create the device object\n");
        return ntStatus;
    }

    // Initialize the driver object with this driver's entry points.
    DriverObject->DriverUnload = DProcMon2UnloadDriver;

    // Initialize a Unicode String containing the Win32 name
    // for our device.
    UNICODE_STRING ntWin32NameString = RTL_CONSTANT_STRING(DOS_DEVICE_NAME);

    // Create a symbolic link between our device name and the Win32 name
    ntStatus = IoCreateSymbolicLink(
        &ntWin32NameString, &ntUnicodeString
    );

    if (!NT_SUCCESS(ntStatus)) {
        // Delete everything that this routine has allocated.
        DPROCMON_KDPRINT("Couldn't create symbolic link\n");
        IoDeleteDevice(deviceObject);
        return ntStatus;
    }

    #if USE_CALLBACKS
    // TODO: Create callback

    ntStatus = PsSetCreateProcessNotifyRoutineEx(DProcMon2OnCreateProcess, FALSE);
    if (!NT_SUCCESS(ntStatus)) {
        // Delete everything that this routine has allocated.
        DPROCMON_KDPRINT("Couldn't set up process creation callback\n");
        IoDeleteSymbolicLink(&ntWin32NameString);
        IoDeleteDevice(deviceObject);
        return ntStatus;
    }
    #endif  // USE_CALLBACKS

    return STATUS_SUCCESS;
}


VOID DProcMon2UnloadDriver(
    _In_ PDRIVER_OBJECT DriverObject
) {
    PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;

    PAGED_CODE();

    #if USE_CALLBACKS
    PsSetCreateProcessNotifyRoutineEx(DProcMon2OnCreateProcess, TRUE);

    // TODO: Destroy callback
    #endif  // USE_CALLBACKS

    // Create counted string version of our Win32 device name.
    UNICODE_STRING uniWin32NameString = RTL_CONSTANT_STRING(DOS_DEVICE_NAME);

    // Delete the link from our device name to a name in the Win32 namespace.
    IoDeleteSymbolicLink(&uniWin32NameString);

    if (deviceObject != NULL) {
        IoDeleteDevice(deviceObject);
    }
}


#if USE_CALLBACKS
void DProcMon2OnCreateProcess(
    PEPROCESS Process,
    HANDLE ProcessId,
    PPS_CREATE_NOTIFY_INFO CreateInfo
) {
    UNREFERENCED_PARAMETER(Process);

    if (!CreateInfo) {
        return;
    }

    if (!CreateInfo->ImageFileName) {
        return;
    }

    struct DPROCMON_INTERNAL_MESSAGE *message = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(struct DPROCMON_INTERNAL_MESSAGE), MEMORY_TAG);
    // Note: we assume ascii process names, but if it isn't, we'll still handle it gracefully
    ANSI_STRING createdProcessName;
    NTSTATUS status = RtlUnicodeStringToAnsiString(&createdProcessName, CreateInfo->ImageFileName, TRUE);
    if (!NT_SUCCESS(status)) {
        DPROCMON_KDPRINT("Failed to convert process name to ANSI: %#08x", status);
        return;
    }

    status = RtlStringCchCopyNA(
        message->CreatedProcessName,
        sizeof(message->CreatedProcessName) - 1,
        createdProcessName.Buffer,
        createdProcessName.Length
    );
    RtlFreeAnsiString(&createdProcessName);  // Regardless of success, we may free the old string now.
    if (!NT_SUCCESS(status)) {
        DPROCMON_KDPRINT("Failed to copy process name to buffer: %#08x", status);
        return;
    }
	
    message->ProcessID = ProcessId;
    
    // TODO: Fire callback
}
#endif  // USE_CALLBACKS
