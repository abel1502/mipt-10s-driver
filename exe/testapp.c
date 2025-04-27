#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <sys\driver.h>


BOOLEAN ManageDriver(
    _In_ BOOLEAN Remove
);

VOID __cdecl main(
    _In_ ULONG argc,
    _In_reads_(argc) PCHAR argv[]
) {
    if (argc >= 2) {
        if (strcmp(argv[1], "install") == 0) {
            if (!ManageDriver(FALSE)) {
                printf("Unable to install driver.\n");

                // Error - remove driver.
                ManageDriver(TRUE);
                return;
            }

            printf("Successfully installed driver.\n");
            return;
        }

        if (strcmp(argv[1], "uninstall") == 0) {
            if (!ManageDriver(TRUE)) {
                printf("Unable to uninstall driver.\n");
                return;
            }

            printf("Successfully uninstalled driver.\n");
            return;
        }

        printf("Ignoring unknown arguments...\n");
    }
    
    HANDLE hDevice;
    if ((hDevice = CreateFile("\\\\.\\DPROCMON", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        printf("Failed to open the driver device: %#08x\n", GetLastError());
        return;
    }

    struct DPROCMON_MESSAGE message;

    while (TRUE) {
        memset(&message, 0, sizeof(message));

        BOOL bRc = DeviceIoControl(hDevice, (DWORD)IOCTL_DPROCMON_GET_SPAWNED_PROCESSES, &message, (DWORD)sizeof(message), &message, (DWORD)sizeof(message), NULL, NULL);
        if (!bRc) {
            printf("Error in DeviceIoControl: %#08x", GetLastError());
            return;
        }

        printf("> %s\n", message.CreatedProcessName);

        if (!message.MoreAvailable) {
            Sleep(1000);
        }
    }

    // close the handle to the device.
    CloseHandle(hDevice);
}
