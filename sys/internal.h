#pragma once

struct LOG_QUEUE_ITEM {
    LIST_ENTRY list_entry;
    PROCESS_NAME process_name;
};

#define NT_DEVICE_NAME  L"\\Device\\DPROCMON"
#define DOS_DEVICE_NAME L"\\DosDevices\\DPPROCMON"

#define MEMORY_TAG 'DPRM'

#if DBG
#define DPROCMON_KDPRINT(...)   \
    DbgPrint("DPROCMON.SYS: "); \
    DbgPrint(__VA_ARGS__);
#else
#define DPROCMON_KDPRINT(...)
#endif


DRIVER_INITIALIZE DriverEntry;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH DProcMonCreateClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH DProcMonDeviceControl;

DRIVER_UNLOAD DProcMonUnloadDriver;

void DProcMonOnCreateProcess(
    PEPROCESS Process,
    HANDLE ProcessId,
    PPS_CREATE_NOTIFY_INFO CreateInfo
);

VOID PrintIrpInfo(PIRP Irp);

VOID PrintChars(
    _In_reads_(CountChars) PCHAR BufferAddress,
    _In_ size_t CountChars
);




#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DProcMonCreateClose)
#pragma alloc_text(PAGE, DProcMonDeviceControl)
#pragma alloc_text(PAGE, DProcMonUnloadDriver)
#pragma alloc_text(PAGE, PrintIrpInfo)
#pragma alloc_text(PAGE, PrintChars)
#endif  // ALLOC_PRAGMA


struct LOG_QUEUE_DATA {
    LIST_ENTRY ListEntry;
    PROCESS_NAME CreatedProcessName;
    HANDLE ProcessHandle;
};


#define LOG_QUEUE_MAX_SIZE  0x80
