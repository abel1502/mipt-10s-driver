#include <windows.h>
#include <winioctl.h>
#include <fltUser.h>
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
    BOOL usePort = FALSE;

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
        } else if (strcmp(argv[1], "uninstall") == 0) {
            printf("Uninstalling driver...\n");

            if (!ManageDriver(TRUE)) {
                printf("Unable to uninstall driver.\n");
                return;
            }

            printf("Successfully uninstalled driver.\n");
            return;
        } else if (strcmp(argv[1], "port") == 0) {
            printf("Using communication port instead of IOCTLs.\n");

            usePort = TRUE;
        } else {
            printf("Unknown arguments, quitting.\n");
            return;
        }
    }

    printf("Starting process monitoring client...\n");
    
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    HANDLE hPort = INVALID_HANDLE_VALUE;

    if (usePort) {
        HRESULT result = FilterConnectCommunicationPort(
            PORT_NAME,
            0,
            NULL,
            0,
            NULL,
            &hPort
        );

        if (FAILED(result)) {
            printf("Failed to connect to port: %#010x\n", result);
            return;
        }
    } else {
        hDevice = CreateFile("\\\\.\\DPROCMON", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        if (hDevice == INVALID_HANDLE_VALUE) {
            printf("Failed to open the driver device: %#010x\n", GetLastError());
            return;
        }
    }

    struct DPROCMON_MESSAGE message;
    memset(&message, 0, sizeof(message));
    BOOL newline = FALSE;

    while (TRUE) {
        DWORD status = 0;

        if (usePort) {
            DWORD bytesReturned = 0;
            status = FilterSendMessage(
                hPort,
                &message,
                (DWORD)sizeof(message),
                &message,
                (DWORD)sizeof(message),
                &bytesReturned  // Unused, but required to be non-null
            );
        } else {
            BOOL bRc = DeviceIoControl(
                hDevice,
                (DWORD)IOCTL_DPROCMON_GET_SPAWNED_PROCESSES,
                &message,
                (DWORD)sizeof(message),
                &message,
                (DWORD)sizeof(message),
                NULL,
                NULL
            );
            if (!bRc) {
                status = GetLastError();
            }
        }

        switch (status) {
        case 0: {
            if (newline) {
                puts("");
                newline = FALSE;
            }

            printf("> %.*s\n", (int)sizeof(message.CreatedProcessName), message.CreatedProcessName);

            if (strcmp(message.CreatedProcessName, "\\??\\C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_11.2501.31.0_x64__8wekyb3d8bbwe\\Notepad\\Notepad.exe") == 0) {
                printf("Requesting termination!\n");
                message.TerminateLast = TRUE;
            }

            if (message.MoreAvailable) {
                continue;
            }
        } break;

        // Apparently minifilters modify the error code via HRESULT_FROM_WIN32
        case (((ERROR_NO_MORE_ITEMS) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000):
        case ERROR_NO_MORE_ITEMS: {
            putchar('.');
            newline = TRUE;
        } break;
        
        default: {
            printf("Error communicating with the driver: %#010x", status);
            return;
        } break;
        }

        Sleep(1000);
    }

    // close the handle to the device.
    CloseHandle(hDevice);
}
