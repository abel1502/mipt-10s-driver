#pragma once

// Device type -- in the "User Defined" range."
#define DPROCMON_TYPE 0x8123

// The IOCTL function codes from 0x800 to 0xFFF are for customer use.
#define IOCTL_DPROCMON_GET_SPAWNED_PROCESSES \
    CTL_CODE(DPROCMON_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define DRIVER_NAME  "DProcMon"

#define PORT_NAME    L"\\DProcMonPort"

typedef CHAR PROCESS_NAME[0x100];

struct DPROCMON_MESSAGE {
    PROCESS_NAME CreatedProcessName;
    BOOLEAN MoreAvailable;
    BOOLEAN TerminateLast;
};
