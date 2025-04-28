#pragma once

struct LOG_QUEUE_ITEM {
    LIST_ENTRY list_entry;
    PROCESS_NAME process_name;
};

#define NT_DEVICE_NAME  L"\\Device\\DPROCMON"
#define DOS_DEVICE_NAME L"\\DosDevices\\DPROCMON"

#define MEMORY_TAG 'DPRM'

#if DBG
#define DPROCMON_KDPRINT(...)   \
    DbgPrint("DPROCMON.SYS: "); \
    DbgPrint(__VA_ARGS__);
#else
#define DPROCMON_KDPRINT(...)
#endif


#pragma region Minifilter
void NTAPI DProcMonPortDisconnectNotify(
    PVOID ConnectionCookie
);

NTSTATUS NTAPI DProcMonPortConnectNotify(
    PFLT_PORT ClientPort,
    PVOID ServerPortCookie,
    PVOID ConnectionContext,
    ULONG SizeOfContext,
    PVOID *ConnectionCookie
);

NTSTATUS NTAPI DProcMonPortMessageNotify(
    PVOID PortCookie,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    ULONG *ReturnOutputBufferLength
);

NTSTATUS DProcMonFilterUnload(
    FLT_FILTER_UNLOAD_FLAGS Flags
);
#pragma endregion Minifilter

DRIVER_INITIALIZE DriverEntry;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH DProcMonCreateClose;

NTSTATUS DProcMonReport(
    ULONG InBufLength,
    PVOID InBuf,
    ULONG OutBufLength,
    PVOID OutBuf,
    ULONG *WrittenLength
);

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH DProcMonDeviceControl;

DRIVER_UNLOAD DProcMonUnloadDriver;

void DProcMonOnCreateProcess(
    PEPROCESS Process,
    HANDLE ProcessId,
    PPS_CREATE_NOTIFY_INFO CreateInfo
);

PLIST_ENTRY MyKeRemoveQueue(PRKQUEUE Queue);

NTSTATUS DProcMonTerminateProcess(HANDLE ProcessID);

VOID PrintIrpInfo(PIRP Irp);


#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DProcMonCreateClose)
#pragma alloc_text(PAGE, DProcMonDeviceControl)
#pragma alloc_text(PAGE, DProcMonUnloadDriver)
#pragma alloc_text(PAGE, PrintIrpInfo)
#endif  // ALLOC_PRAGMA


struct LOG_QUEUE_DATA {
    LIST_ENTRY ListEntry;
    PROCESS_NAME CreatedProcessName;
    HANDLE ProcessID;
};


#define LOG_QUEUE_MAX_SIZE  0x80
