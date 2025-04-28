#pragma once

#define NT_DEVICE_NAME  L"\\Device\\DPROCMON2"
#define DOS_DEVICE_NAME L"\\DosDevices\\DPROCMON2"

#define MEMORY_TAG 'DPM2'

#if DBG
#define DPROCMON_KDPRINT(...)   \
    DbgPrint("DPROCMON.SYS: " __VA_ARGS__);
#else
#define DPROCMON_KDPRINT(...)
#endif

DRIVER_INITIALIZE DriverEntry;

DRIVER_UNLOAD DProcMon2UnloadDriver;

#if USE_CALLBACKS
void DProcMon2OnCreateProcess(
    PEPROCESS Process,
    HANDLE ProcessId,
    PPS_CREATE_NOTIFY_INFO CreateInfo
);
#endif  // USE_CALLBACKS

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DProcMon2UnloadDriver)
#endif  // ALLOC_PRAGMA
