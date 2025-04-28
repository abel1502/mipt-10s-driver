#include <ntifs.h>
#include <ntstrsafe.h>
#include <ntddk.h>
#include <fltKernel.h>
#include <string.h>

#include "driver.h"
#include "internal.h"


static KQUEUE g_LogQueue;
static volatile LONG g_LoqQueueSize;
static volatile HANDLE g_LastReportedProcessID;

#pragma region Minifilter
static PFLT_FILTER g_FilterHandle = NULL;
static PFLT_PORT g_ServerPort = NULL;
static volatile PFLT_PORT g_ClientPort = NULL;

static const FLT_OPERATION_REGISTRATION Callbacks[] = {
    {IRP_MJ_OPERATION_END}
};

static const FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),  // Size
    FLT_REGISTRATION_VERSION,  // Version
    0,                         // Flags
    NULL,                      // Context
    Callbacks,                 // Operation callbacks (none)
    NULL,                      // MiniFilterUnload (optional, you can provide one)
    NULL,                      // InstanceSetup
    NULL,                      // InstanceQueryTeardown
    NULL,                      // InstanceTeardownStart
    NULL,                      // InstanceTeardownComplete
    NULL,                      // GenerateFileName
    NULL,                      // GenerateDestinationFileName
    NULL                       // NormalizeNameComponent
};

void NTAPI DProcMonPortDisconnectNotify(
    PVOID ConnectionCookie
) {
    UNREFERENCED_PARAMETER(ConnectionCookie);

    PFLT_PORT clientPort = InterlockedExchangePointer(&g_ClientPort, NULL);
    if (clientPort) {
        FltCloseClientPort(g_FilterHandle, &clientPort);
    }
}

NTSTATUS NTAPI DProcMonPortConnectNotify(
    PFLT_PORT ClientPort,
    PVOID ServerPortCookie,
    PVOID ConnectionContext,
    ULONG SizeOfContext,
    PVOID *ConnectionCookie
) {
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionCookie);

    PFLT_PORT OldClientPort = InterlockedCompareExchangePointer(&g_ClientPort, ClientPort, NULL);

    if (OldClientPort != NULL) {
        return STATUS_DEVICE_BUSY;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DProcMonPortMessageNotify(
    PVOID PortCookie,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    ULONG *ReturnOutputBufferLength
) {
    UNREFERENCED_PARAMETER(PortCookie);

    return DProcMonReport(
        InputBufferLength,
        InputBuffer,
        OutputBufferLength,
        OutputBuffer,
        ReturnOutputBufferLength
    );
}
#pragma endregion Minifilter


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
    InterlockedExchange(&g_LoqQueueSize, 0);
    InterlockedExchangePointer(&g_LastReportedProcessID, NULL);

    ntStatus = PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, FALSE);
    if (!NT_SUCCESS(ntStatus)) {
        // Delete everything that this routine has allocated.
        DPROCMON_KDPRINT("Couldn't set up process creation callback\n");
        IoDeleteSymbolicLink(&ntWin32NameString);
        IoDeleteDevice(deviceObject);
        return ntStatus;
    }

    // Register a dummy filter
    ntStatus = FltRegisterFilter(DriverObject, &FilterRegistration, &g_FilterHandle);
    if (!NT_SUCCESS(ntStatus)) {
        // Delete everything that this routine has allocated.
        DPROCMON_KDPRINT("Couldn't register filter\n");
        PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, TRUE);
        IoDeleteSymbolicLink(&ntWin32NameString);
        IoDeleteDevice(deviceObject);
        return ntStatus;
    }

    UNICODE_STRING portName = RTL_CONSTANT_STRING(L"\\DProcMonPort");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &portName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    ntStatus = FltCreateCommunicationPort(
        g_FilterHandle,
        &g_ServerPort,
        &oa,
        NULL,
        DProcMonPortConnectNotify,
        DProcMonPortDisconnectNotify,
        DProcMonPortMessageNotify,
        1  // Max connections
    );

    if (!NT_SUCCESS(ntStatus)) {
        // Delete everything that this routine has allocated.
        DPROCMON_KDPRINT("Couldn't create filter communication port\n");
        FltUnregisterFilter(g_FilterHandle);
        PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, TRUE);
        IoDeleteSymbolicLink(&ntWin32NameString);
        IoDeleteDevice(deviceObject);
        return ntStatus;
    }

    // Start the filter manager (we don't actually really filter anything)
    ntStatus = FltStartFiltering(g_FilterHandle);
    if (!NT_SUCCESS(ntStatus)) {
        DPROCMON_KDPRINT("Couldn't start dummy filter\n");
        FltCloseCommunicationPort(g_ServerPort);
        FltUnregisterFilter(g_FilterHandle);
        PsSetCreateProcessNotifyRoutineEx(DProcMonOnCreateProcess, TRUE);
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

    if (g_FilterHandle) {
        FltUnregisterFilter(g_FilterHandle);
    }

    if (g_ServerPort) {
        FltCloseCommunicationPort(g_ServerPort);
    }

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

    if (irpSp->Parameters.DeviceIoControl.IoControlCode != IOCTL_DPROCMON_GET_SPAWNED_PROCESSES) {
        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        DPROCMON_KDPRINT("ERROR: unrecognized IOCTL %#x\n", irpSp->Parameters.DeviceIoControl.IoControlCode);
        goto End;
    }

    #if DBG && FALSE
    DPROCMON_KDPRINT("Called IOCTL_DPROCMON_GET_SPAWNED_PROCESSES\n");
    PrintIrpInfo(Irp);
    #endif

    ULONG Written = 0;
    ntStatus = DProcMonReport(
        irpSp->Parameters.DeviceIoControl.InputBufferLength,
        Irp->AssociatedIrp.SystemBuffer,
        irpSp->Parameters.DeviceIoControl.OutputBufferLength,
        Irp->AssociatedIrp.SystemBuffer,
        &Written
    );
    Irp->IoStatus.Information = Written;

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


NTSTATUS DProcMonReport(
    ULONG InBufLength,
    PVOID InBuf,
    ULONG OutBufLength,
    PVOID OutBuf,
    ULONG *WrittenLength
) {
    if (InBufLength < sizeof(struct DPROCMON_MESSAGE) || OutBufLength < sizeof(struct DPROCMON_MESSAGE)) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = STATUS_SUCCESS;
    struct DPROCMON_MESSAGE message = *(struct DPROCMON_MESSAGE *)InBuf;

    if (message.TerminateLast) {
        DPROCMON_KDPRINT("Reqested termination of last reported process\n");

        message.TerminateLast = FALSE;

        HANDLE ProcessID = InterlockedExchangePointer(&g_LastReportedProcessID, NULL);

        // If the process is already dead, this isn't an error.
        status = DProcMonTerminateProcess(ProcessID);

        if (!NT_SUCCESS(status)) {
            goto End;
        }
    }

    RtlZeroMemory(&message, sizeof(message));

    PLIST_ENTRY listEntry = MyKeRemoveQueue(&g_LogQueue);
    if (!listEntry) {
        message.CreatedProcessName[0] = 0;
        message.MoreAvailable = FALSE;

        status = STATUS_NO_MORE_ENTRIES;
        goto End;
    }

    struct LOG_QUEUE_DATA *data = CONTAINING_RECORD(listEntry, struct LOG_QUEUE_DATA, ListEntry);

    RtlCopyMemory(message.CreatedProcessName, data->CreatedProcessName, sizeof(data->CreatedProcessName));
    InterlockedExchangePointer(&g_LastReportedProcessID, data->ProcessID);

    ExFreePoolWithTag(data, MEMORY_TAG);

    LONG remainingItems = InterlockedDecrement(&g_LoqQueueSize);
    message.MoreAvailable = (remainingItems > 0);

    status = STATUS_SUCCESS;

End:
    *(struct DPROCMON_MESSAGE *)OutBuf = message;

    if (WrittenLength) {
        *WrittenLength = sizeof(message);
    }

    return status;
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
	
    data->ProcessID = ProcessId;
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


NTSTATUS DProcMonTerminateProcess(HANDLE ProcessID) {
    if (ProcessID == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS Process;
    NTSTATUS status = PsLookupProcessByProcessId(ProcessID, &Process);
    if (status == STATUS_INVALID_CID) {
        // If a process is already dead, it's considered a success
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }

    HANDLE hProcess;
    status = ObOpenObjectByPointer(
        Process,
        OBJ_KERNEL_HANDLE,
        NULL,
        0x0001,  // PROCESS_TERMINATE
        *PsProcessType,
        KernelMode,
        &hProcess
    );
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(Process);
        return status;
    }

    status = ZwTerminateProcess(hProcess, STATUS_SUCCESS);
    
    ZwClose(hProcess);
    ObDereferenceObject(Process);

    return status;
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
