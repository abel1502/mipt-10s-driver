#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <sys\driver.h>


BOOLEAN SetupDriverName(
    _Inout_updates_bytes_all_(BufferLength) PCHAR DriverLocation,
    _In_ ULONG BufferLength
) {
    HANDLE fileHandle;
    DWORD driverLocLen = 0;

    // Get the current directory.
    driverLocLen = GetCurrentDirectory(BufferLength, DriverLocation);

    if (driverLocLen == 0) {
        printf("GetCurrentDirectory failed! Error = %d \n", GetLastError());
        return FALSE;
    }

    // Setup path name to driver file.
    if (FAILED(StringCbCat(DriverLocation, BufferLength, "\\" DRIVER_NAME ".sys"))) {
        return FALSE;
    }

    // Ensure driver file is in the specified directory.
    if ((fileHandle = CreateFile(DriverLocation, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        printf("%s.sys is not loaded.\n", DRIVER_NAME);
        return FALSE;
    }

    // Close the file handle
    if (fileHandle) {
        CloseHandle(fileHandle);
    }

    return TRUE;
}

BOOLEAN InstallDriver(
    _In_ SC_HANDLE SchSCManager
) {
    DWORD err;

    //
    // NOTE: This creates an entry for a standalone driver. If this
    //       is modified for use with a driver that requires a Tag,
    //       Group, and/or Dependencies, it may be necessary to
    //       query the registry for existing driver information
    //       (in order to determine a unique Tag, etc.).
    //

    CHAR ServiceExe[MAX_PATH];
    if (!SetupDriverName(ServiceExe, sizeof(ServiceExe))) {
        return FALSE;
    }

    // Create a new a service object.
    SC_HANDLE schService = CreateService(
        SchSCManager,           // handle of service control manager database
        DRIVER_NAME,            // address of name of service to start
        DRIVER_NAME,            // address of display name
        SERVICE_ALL_ACCESS,     // type of access to service
        SERVICE_KERNEL_DRIVER,  // type of service
        SERVICE_DEMAND_START,   // when to start service
        SERVICE_ERROR_NORMAL,   // severity if service fails to start
        ServiceExe,             // address of name of binary file
        NULL,                   // service does not belong to a group
        NULL,                   // no tag requested
        NULL,                   // no dependency names // note: double 0 byte at the end required!
        NULL,                   // use LocalSystem account
        NULL                    // no password for service account
    );

    if (schService == NULL) {
        err = GetLastError();

        if (err != ERROR_SERVICE_EXISTS) {
            printf("CreateService failed! Error = %d \n", err);
            return FALSE;
        }

        // Ignore this error.
        return TRUE;
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return TRUE;

}

BOOLEAN RemoveDriver(
    _In_ SC_HANDLE SchSCManager
) {
    BOOLEAN rCode;

    // Open the handle to the existing service.
    SC_HANDLE schService = OpenService(SchSCManager, DRIVER_NAME, SERVICE_ALL_ACCESS);

    if (schService == NULL) {
        printf("OpenService failed! Error = %d \n", GetLastError());
        return FALSE;
    }

    // Mark the service for deletion from the service control manager database.
    if (DeleteService(schService)) {
        rCode = TRUE;
    } else {
        printf("DeleteService failed! Error = %d \n", GetLastError());
        rCode = FALSE;
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return rCode;
}


BOOLEAN StartDriver(
    _In_ SC_HANDLE SchSCManager
) {
    DWORD err;

    // Open the handle to the existing service.
    SC_HANDLE schService = OpenService(SchSCManager, DRIVER_NAME, SERVICE_ALL_ACCESS);

    if (schService == NULL) {
        printf("OpenService failed! Error = %d \n", GetLastError());
        return FALSE;
    }

    // Start the execution of the service (i.e. start the driver).
    if (!StartService(schService, 0, NULL)) {  // svc, argc, argv

        err = GetLastError();

        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            // Ignore this error.
            return TRUE;
        } else {
            printf("StartService failure! Error = %d \n", err);
            return FALSE;
        }
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return TRUE;
}


BOOLEAN StopDriver(
    _In_ SC_HANDLE SchSCManager
) {
    BOOLEAN rCode = TRUE;
    SERVICE_STATUS serviceStatus;

    // Open the handle to the existing service.
    SC_HANDLE schService = OpenService(SchSCManager, DRIVER_NAME, SERVICE_ALL_ACCESS);

    if (schService == NULL) {
        printf("OpenService failed! Error = %d \n", GetLastError());
        return FALSE;
    }

    // Request that the service stop.
    if (ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus)) {
        rCode = TRUE;
    } else {
        printf("ControlService failed! Error = %d \n", GetLastError());
        rCode = FALSE;
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return rCode;
}

BOOLEAN ManageDriver(
    _In_ BOOLEAN Remove
) {
    BOOLEAN rCode = TRUE;

    // Connect to the Service Control Manager and open the Services database.
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (!schSCManager) {
        printf("Open SC Manager failed! Error = %d \n", GetLastError());
        return FALSE;
    }

    // Do the requested function.
    switch (Remove) {
    case FALSE:
        // Install the driver service.
        if (InstallDriver(schSCManager)) {
            // Start the driver service (i.e. start the driver).
            rCode = StartDriver(schSCManager);
        } else {
            rCode = FALSE;
        }

        break;

    case TRUE:
        // Stop the driver.
        StopDriver(schSCManager);

        // Remove the driver service.
        RemoveDriver(schSCManager);

        // Ignore all errors.
        rCode = TRUE;
        break;
    }

    // Close handle to service control manager.
    if (schSCManager) {
        CloseServiceHandle(schSCManager);
    }

    return rCode;
}
