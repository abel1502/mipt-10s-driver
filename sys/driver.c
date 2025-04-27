#include <ntddk.h>
#include <Ntifs.h>
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

    return STATUS_SUCCESS;
}


VOID DProcMonUnloadDriver(
    _In_ PDRIVER_OBJECT DriverObject
) {
    PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;

    PAGED_CODE();

    PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, TRUE);

    // Create counted string version of our Win32 device name.
    UNICODE_STRING uniWin32NameString = RTL_CONSTANT_STRING(DOS_DEVICE_NAME);

    // Delete the link from our device name to a name in the Win32 namespace.
    IoDeleteSymbolicLink(&uniWin32NameString);

    if (deviceObject != NULL) {
        IoDeleteDevice(deviceObject);
    }

    for (
        PLIST_ENTRY listEntry = KeRemoveQueue(&g_LogQueue, KernelMode, NULL);
        listEntry != NULL;
        listEntry = KeRemoveQueue(&g_LogQueue, KernelMode, NULL)
    ) {
        struct LOG_QUEUE_DATA *data = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);
        ExFreePoolWithTag(data, MEMORY_TAG);
        InterlockedDecrement(&g_LoqQueueSize);
    }
}


NTSTATUS DProcMonDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
) {
    NTSTATUS ntStatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);

    // InterlockedExchangePointer(&g_LastReportedProcess, ProcessId);
    // InterlockedCompareExchangePointer(&g_LastReportedProcess, NULL, NULL);

    //PLIST_ENTRY listEntry = KeRemoveQueue(&g_LogQueue, KernelMode, NULL);
    //if (listEntry) {
    //    struct LOG_QUEUE_DATA *data = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);
    //    // TODO: Copy to user
    //    ExFreePool(data);
    //    return STATUS_SUCCESS;
    //} else {
    //    return STATUS_NO_MORE_ENTRIES;
    //}

    PAGED_CODE();

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG inBufLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outBufLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (!inBufLength || !outBufLength) {
        ntStatus = STATUS_INVALID_PARAMETER;
        goto End;
    }

    if (irpSp->Parameters.DeviceIoControl.IoControlCode != IOCTL_DPROCMON_GET_SPAWNED_PROCESSES) {
        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        DPROCMON_KDPRINT("ERROR: unrecognized IOCTL %x\n", irpSp->Parameters.DeviceIoControl.IoControlCode);
        goto End;
    }

    DPROCMON_KDPRINT("Called IOCTL_DPROCMON_GET_SPAWNED_PROCESSES\n");
    PrintIrpInfo(Irp);

    PCHAR buf = Irp->AssociatedIrp.SystemBuffer;

    // Assign the length of the data copied to IoStatus.Information
    // of the Irp and complete the Irp.
    Irp->IoStatus.Information = outBufLength;

End:
    // Finish the I/O operation by simply completing the packet and returning
    // the same status as in the packet itself.
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
    // Fill myData
    KeInsertQueue(&g_LogQueue, &data->ListEntry);

    LONG queueSize = InterlockedIncrement(&g_LoqQueueSize);
    if (queueSize > LOG_QUEUE_MAX_SIZE) {
        // If the queue grows too big, pop the extra item
        PLIST_ENTRY listEntry = KeRemoveQueue(&g_LogQueue, KernelMode, NULL);
        if (listEntry) {
            struct LOG_QUEUE_DATA *data = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);
            ExFreePoolWithTag(data, MEMORY_TAG);
            InterlockedDecrement(&g_LoqQueueSize);
        }
    }
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
