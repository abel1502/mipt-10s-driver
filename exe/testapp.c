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
            printf("Installing driver...\n");

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
            printf("Uninstalling driver...\n");

            if (!ManageDriver(TRUE)) {
                printf("Unable to uninstall driver.\n");
                return;
            }

            printf("Successfully uninstalled driver.\n");
            return;
        }

        printf("Ignoring unknown arguments...\n");
    }

    printf("Starting process monitoring client...\n");
    
    HANDLE hDevice;
    if ((hDevice = CreateFile("\\\\.\\DPROCMON", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        printf("Failed to open the driver device: %#010x\n", GetLastError());
        return;
    }

    struct DPROCMON_MESSAGE message;
    BOOL newline = FALSE;

    while (TRUE) {
        memset(&message, 0, sizeof(message));

        BOOL bRc = DeviceIoControl(hDevice, (DWORD)IOCTL_DPROCMON_GET_SPAWNED_PROCESSES, &message, (DWORD)sizeof(message), &message, (DWORD)sizeof(message), NULL, NULL);
        DWORD status = 0;
        if (!bRc) {
            status = GetLastError();
        }

        switch (status) {
        case 0: {
            if (newline) {
                puts("");
                newline = FALSE;
            }

            printf("> %s\n", message.CreatedProcessName);

            if (message.MoreAvailable) {
                continue;
            }
        } break;

        case ERROR_NO_MORE_ITEMS: {
            putchar('.');
            newline = TRUE;
        } break;

        default: {
            printf("Error in DeviceIoControl: %#010x", GetLastError());
            return;
        } break;
        }

        Sleep(1000);
    }

    // close the handle to the device.
    CloseHandle(hDevice);
}
