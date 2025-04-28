#pragma once

// Device type -- in the "User Defined" range."
#define DPROCMON_TYPE 0x8123

// The IOCTL function codes from 0x800 to 0xFFF are for customer use.
#define IOCTL_DPROCMON_GET_SPAWNED_PROCESSES \
    CTL_CODE(DPROCMON_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define DRIVER_NAME  "DProcMon"
#define DRIVER2_NAME "DProcMon2"

#define PORT_NAME    L"\\DProcMonPort"

// If 0, the process detection happens in driver 1.
// If 1, the detection happens in driver 2 and information in
// transmitted via a callback.
// Note that the second driver is still installed and
// launched regardless of the value of this option
#define USE_CALLBACKS 0

typedef CHAR PROCESS_NAME[0x100];

struct DPROCMON_MESSAGE {
    PROCESS_NAME CreatedProcessName;
    BOOLEAN MoreAvailable;
    BOOLEAN TerminateLast;
};

struct DPROCMON_INTERNAL_MESSAGE {
    PROCESS_NAME CreatedProcessName;
    HANDLE ProcessID;
};
