#include <ntifs.h>
#include <ntstrsafe.h>
#include <ntddk.h>
#include <string.h>

#include "driver.h"
#include "internal.h"


static KQUEUE g_LogQueue;
static volatile LONG g_LoqQueueSize = 0;
static volatile HANDLE g_LastReportedProcess = NULL;


NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING ntUnicodeString = RTL_CONSTANT_STRING(NT_DEVICE_NAME);

    PDEVICE_OBJECT deviceObject = NULL;  // ptr to device object
    NTSTATUS ntStatus = IoCreateDevice(
        DriverObject,             // Our Driver Object
        0,                        // We don't use a device extension
        &ntUnicodeString,         // Device name "\Device\DPROCMON"
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
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DProcMonCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DProcMonCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DProcMonDeviceControl;
    DriverObject->DriverUnload = DProcMonUnloadDriver;

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

    KeInitializeQueue(&g_LogQueue, 0);

    ntStatus = PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, FALSE);
    if (!NT_SUCCESS(ntStatus)) {
        // Delete everything that this routine has allocated.
        DPROCMON_KDPRINT("Couldn't set up process creation callback\n");
        IoDeleteSymbolicLink(&ntWin32NameString);
        IoDeleteDevice(deviceObject);
        return ntStatus;
    }

    return STATUS_SUCCESS;
}


VOID DProcMonUnloadDriver(
    _In_ PDRIVER_OBJECT DriverObject
) {
    PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;

    PAGED_CODE();

    PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, TRUE);

    KeRundownQueue(&g_LogQueue);

    for (
        PLIST_ENTRY listEntry = MyKeRemoveQueue(&g_LogQueue);
        listEntry;
        listEntry = MyKeRemoveQueue(&g_LogQueue)
    ) {
        struct LOG_QUEUE_DATA *data = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);
        ExFreePoolWithTag(data, MEMORY_TAG);
        InterlockedDecrement(&g_LoqQueueSize);
    }

    // Create counted string version of our Win32 device name.
    UNICODE_STRING uniWin32NameString = RTL_CONSTANT_STRING(DOS_DEVICE_NAME);

    // Delete the link from our device name to a name in the Win32 namespace.
    IoDeleteSymbolicLink(&uniWin32NameString);

    if (deviceObject != NULL) {
        IoDeleteDevice(deviceObject);
    }
}


NTSTATUS DProcMonDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
) {
    NTSTATUS ntStatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    ULONG inBufLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outBufLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    if (inBufLength < sizeof(struct DPROCMON_MESSAGE) || outBufLength < sizeof(struct DPROCMON_MESSAGE)) {
        ntStatus = STATUS_INVALID_PARAMETER;
        goto End;
    }

    if (irpSp->Parameters.DeviceIoControl.IoControlCode != IOCTL_DPROCMON_GET_SPAWNED_PROCESSES) {
        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        DPROCMON_KDPRINT("ERROR: unrecognized IOCTL %#x\n", irpSp->Parameters.DeviceIoControl.IoControlCode);
        goto End;
    }

    #if DBG
    DPROCMON_KDPRINT("Called IOCTL_DPROCMON_GET_SPAWNED_PROCESSES\n");
    PrintIrpInfo(Irp);
    #endif

    struct DPROCMON_MESSAGE *message = Irp->AssociatedIrp.SystemBuffer;

    if (message->TerminateLast) {
        DPROCMON_KDPRINT("Reqested termination of last reported process\n");
        DPROCMON_KDPRINT("(Not yet implemented, ignored)\n");

        // InterlockedCompareExchangePointer(&g_LastReportedProcess, NULL, NULL);
    }

    RtlZeroMemory(message, sizeof(*message));

    PLIST_ENTRY listEntry = MyKeRemoveQueue(&g_LogQueue);
    if (!listEntry) {
        message->CreatedProcessName[0] = 0;
        message->MoreAvailable = FALSE;

        ntStatus = STATUS_NO_MORE_ENTRIES;
        goto End;
    }

    struct LOG_QUEUE_DATA *data = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);

    RtlCopyMemory(message->CreatedProcessName, data->CreatedProcessName, sizeof(data->CreatedProcessName));
    InterlockedExchangePointer(&g_LastReportedProcess, data->ProcessHandle);

    ExFreePoolWithTag(data, MEMORY_TAG);

    LONG remainingItems = InterlockedDecrement(&g_LoqQueueSize);
    message->MoreAvailable = (remainingItems > 0);

    ntStatus = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*message);

End:
    Irp->IoStatus.Status = ntStatus;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return ntStatus;
}


NTSTATUS DProcMonCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


void DProcMonOnCreateProcess(
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

    struct LOG_QUEUE_DATA *data = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(struct LOG_QUEUE_DATA), MEMORY_TAG);
    // Note: we assume ascii process names, but if it isn't, we'll still handle it gracefully
    ANSI_STRING createdProcessName;
    NTSTATUS status = RtlUnicodeStringToAnsiString(&createdProcessName, CreateInfo->ImageFileName, TRUE);
    if (!NT_SUCCESS(status)) {
        DPROCMON_KDPRINT("Failed to convert process name to ANSI: %#08x", status);
        return;
    }

    status = RtlStringCchCopyNA(
        data->CreatedProcessName,
        sizeof(data->CreatedProcessName) - 1,
        createdProcessName.Buffer,
        createdProcessName.Length
    );
    RtlFreeAnsiString(&createdProcessName);  // Regardless of success, we may free the old string now.
    if (!NT_SUCCESS(status)) {
        DPROCMON_KDPRINT("Failed to copy process name to buffer: %#08x", status);
        return;
    }
	
    data->ProcessHandle = ProcessId;
    KeInsertQueue(&g_LogQueue, &data->ListEntry);

    LONG queueSize = InterlockedIncrement(&g_LoqQueueSize);
    if (queueSize > LOG_QUEUE_MAX_SIZE) {
        // If the queue grows too big, pop the extra item
        PLIST_ENTRY listEntry = MyKeRemoveQueue(&g_LogQueue);
        if (listEntry) {
            struct LOG_QUEUE_DATA *head = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);
            ExFreePoolWithTag(head, MEMORY_TAG);
            InterlockedDecrement(&g_LoqQueueSize);
        }
    }
}


PLIST_ENTRY MyKeRemoveQueue(PRKQUEUE Queue) {
    LARGE_INTEGER ZeroTimeout = {
        .QuadPart = 0
    };

    PLIST_ENTRY listEntry = KeRemoveQueue(Queue, KernelMode, &ZeroTimeout);

    ULONG_PTR status = (ULONG_PTR)listEntry;

    if (status == STATUS_TIMEOUT || status == STATUS_USER_APC || status == STATUS_ABANDONED) {
        return NULL;
    }

    return listEntry;
}


VOID PrintIrpInfo(
    PIRP Irp
) {
    PIO_STACK_LOCATION irpSp;
    irpSp = IoGetCurrentIrpStackLocation(Irp);

    PAGED_CODE();

    DPROCMON_KDPRINT("\tIrp->AssociatedIrp.SystemBuffer = 0x%p\n", Irp->AssociatedIrp.SystemBuffer);
    DPROCMON_KDPRINT("\tIrp->UserBuffer = 0x%p\n", Irp->UserBuffer);
    DPROCMON_KDPRINT("\tirpSp->Parameters.DeviceIoControl.Type3InputBuffer = 0x%p\n", irpSp->Parameters.DeviceIoControl.Type3InputBuffer);
    DPROCMON_KDPRINT("\tirpSp->Parameters.DeviceIoControl.InputBufferLength = %d\n", irpSp->Parameters.DeviceIoControl.InputBufferLength);
    DPROCMON_KDPRINT("\tirpSp->Parameters.DeviceIoControl.OutputBufferLength = %d\n", irpSp->Parameters.DeviceIoControl.OutputBufferLength);
    
    return;
}
