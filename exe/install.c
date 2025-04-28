#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <sys\driver.h>


BOOLEAN SetupDriverName(
    _Inout_updates_bytes_all_(BufferLength) PCHAR DriverLocation,
    _In_ ULONG BufferLength,
    _In_ LPCSTR DriverName
) {
    HANDLE fileHandle;
    DWORD driverLocLen = 0;

    // Get the current directory.
    driverLocLen = GetCurrentDirectory(BufferLength, DriverLocation);

    if (driverLocLen == 0) {
        printf("GetCurrentDirectory failed! Error: %#010x\n", GetLastError());
        return FALSE;
    }

    // Setup path name to driver file.
    if (FAILED(StringCbCat(DriverLocation, BufferLength, "\\"))) {
        return FALSE;
    }

    if (FAILED(StringCbCat(DriverLocation, BufferLength, DriverName))) {
        return FALSE;
    }

    if (FAILED(StringCbCat(DriverLocation, BufferLength, ".sys"))) {
        return FALSE;
    }


    // Ensure driver file is in the specified directory.
    if ((fileHandle = CreateFile(DriverLocation, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        printf("%s.sys is not loaded.\n", DriverName);
        return FALSE;
    }

    // Close the file handle
    if (fileHandle) {
        CloseHandle(fileHandle);
    }

    return TRUE;
}

BOOLEAN CreateFilterRegistryKeys(LPCSTR serviceName) {
    HKEY hInstancesKey = NULL;
    HKEY hInstanceSubkey = NULL;
    LONG status;
    DWORD value;
    const CHAR *defaultInstanceName = "DProcMonFilterInstance";
    const CHAR *altitude = "380001";  // An unused value in the correct range

    CHAR instancesPath[512];
    snprintf(instancesPath, 512, "SYSTEM\\CurrentControlSet\\Services\\%s\\Instances", serviceName);

    // Create/open Instances key
    status = RegCreateKeyEx(
        HKEY_LOCAL_MACHINE,
        instancesPath,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        NULL,
        &hInstancesKey,
        NULL
    );
    if (status != ERROR_SUCCESS) {
        printf("Failed to create/open Instances key: %ld\n", status);
        return FALSE;
    }

    // Set DefaultInstance value
    status = RegSetValueEx(
        hInstancesKey,
        "DefaultInstance",
        0,
        REG_SZ,
        (const BYTE *)defaultInstanceName,
        (DWORD)((strlen(defaultInstanceName) + 1) * sizeof(CHAR))
    );
    if (status != ERROR_SUCCESS) {
        printf("Failed to set DefaultInstance: %ld\n", status);
        RegCloseKey(hInstancesKey);
        return FALSE;
    }

    // Create instance subkey
    status = RegCreateKeyEx(
        hInstancesKey,
        defaultInstanceName,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        NULL,
        &hInstanceSubkey,
        NULL
    );
    if (status != ERROR_SUCCESS) {
        printf("Failed to create instance subkey: %ld\n", status);
        RegCloseKey(hInstancesKey);
        return FALSE;
    }

    // Set Altitude
    status = RegSetValueEx(
        hInstanceSubkey,
        "Altitude",
        0,
        REG_SZ,
        (const BYTE *)altitude,
        (DWORD)((strlen(altitude) + 1) * sizeof(CHAR))
    );
    if (status != ERROR_SUCCESS) {
        printf("Failed to set Altitude: %ld\n", status);
        RegCloseKey(hInstanceSubkey);
        RegCloseKey(hInstancesKey);
        return FALSE;
    }

    // Set Flags (usually 0)
    value = 0;
    status = RegSetValueEx(
        hInstanceSubkey,
        "Flags",
        0,
        REG_DWORD,
        (const BYTE *)&value,
        sizeof(DWORD)
    );
    if (status != ERROR_SUCCESS) {
        printf("Failed to set Flags: %ld\n", status);
        RegCloseKey(hInstanceSubkey);
        RegCloseKey(hInstancesKey);
        return FALSE;
    }

    RegCloseKey(hInstanceSubkey);
    RegCloseKey(hInstancesKey);

    return TRUE;
}

BOOLEAN InstallDriver(
    _In_ SC_HANDLE SchSCManager,
    _In_ LPCSTR ServiceName,
    _In_opt_ LPCSTR Dependencies
) {
    DWORD err;

    // NOTE: This creates an entry for a standalone driver. If this
    //       is modified for use with a driver that requires a Tag,
    //       Group, and/or Dependencies, it may be necessary to
    //       query the registry for existing driver information
    //       (in order to determine a unique Tag, etc.).
    CHAR ServiceExe[MAX_PATH];
    if (!SetupDriverName(ServiceExe, sizeof(ServiceExe), ServiceName)) {
        return FALSE;
    }

    // Create a new a service object.
    SC_HANDLE schService = CreateService(
        SchSCManager,           // handle of service control manager database
        ServiceName,            // address of name of service to start
        ServiceName,            // address of display name
        SERVICE_ALL_ACCESS,     // type of access to service
        SERVICE_KERNEL_DRIVER,  // type of service
        SERVICE_DEMAND_START,   // when to start service
        SERVICE_ERROR_NORMAL,   // severity if service fails to start
        ServiceExe,             // address of name of binary file
        NULL,                   // service does not belong to a group
        NULL,                   // no tag requested
        Dependencies,           // dependency names, note: double 0 byte at the end required!
        NULL,                   // use LocalSystem account
        NULL                    // no password for service account
    );

    if (schService == NULL) {
        err = GetLastError();

        if (err == ERROR_SERVICE_EXISTS) {
            // Ignore this error.
            return TRUE;
        }

        printf("CreateService failed! Error: %#010x\n", err);
        return FALSE;
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return TRUE;
}

BOOLEAN RemoveDriver(
    _In_ SC_HANDLE SchSCManager,
    _In_ LPCSTR ServiceName
) {
    BOOLEAN rCode;

    // Open the handle to the existing service.
    SC_HANDLE schService = OpenService(SchSCManager, ServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL) {
        printf("OpenService failed! Error: %#010x\n", GetLastError());
        return FALSE;
    }

    // Mark the service for deletion from the service control manager database.
    if (DeleteService(schService)) {
        rCode = TRUE;
    } else {
        printf("DeleteService failed! Error: %#010x\n", GetLastError());
        rCode = FALSE;
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return rCode;
}


BOOLEAN StartDriver(
    _In_ SC_HANDLE SchSCManager,
    _In_ LPCSTR ServiceName
) {
    DWORD err;

    // Open the handle to the existing service.
    SC_HANDLE schService = OpenService(SchSCManager, ServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL) {
        printf("OpenService failed! Error: %#010x\n", GetLastError());
        return FALSE;
    }

    // Start the execution of the service (i.e. start the driver).
    if (!StartService(schService, 0, NULL)) {  // svc, argc, argv

        err = GetLastError();

        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            // Ignore this error.
            return TRUE;
        }

        printf("StartService failure! Error: %#010x\n", err);
        return FALSE;
    }

    // Close the service object.
    if (schService) {
        CloseServiceHandle(schService);
    }

    return TRUE;
}


BOOLEAN StopDriver(
    _In_ SC_HANDLE SchSCManager,
    _In_ LPCSTR ServiceName
) {
    BOOLEAN rCode = TRUE;
    SERVICE_STATUS serviceStatus;

    // Open the handle to the existing service.
    SC_HANDLE schService = OpenService(SchSCManager, ServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL) {
        printf("OpenService failed! Error: %#010x\n", GetLastError());
        return FALSE;
    }

    // Request that the service stop.
    if (ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus)) {
        rCode = TRUE;
    } else {
        printf("ControlService failed! Error: %#010x\n", GetLastError());
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
        printf("Open SC Manager failed! Error: %#010x\n", GetLastError());
        return FALSE;
    }

    // Do the requested function.
    switch (Remove) {
    case FALSE:
        // Install the driver service.
        if (!InstallDriver(schSCManager, DRIVER2_NAME, NULL)) {
            rCode = FALSE;
            break;
        }

        // Install the second driver service.
        if (!InstallDriver(schSCManager, DRIVER_NAME, "FltMgr\0" DRIVER2_NAME "\0\0")) {
            RemoveDriver(schSCManager, DRIVER2_NAME);
            rCode = FALSE;
            break;
        }

        // Create the filter registry keys for the first driver.
        if (!CreateFilterRegistryKeys(DRIVER_NAME)) {
            RemoveDriver(schSCManager, DRIVER_NAME);
            RemoveDriver(schSCManager, DRIVER2_NAME);
            rCode = FALSE;
            break;
        }

        // Wait for the installation to complete...?
        Sleep(1000);

        // Start the driver services (i.e. start the driver).
        // The second driver should start automatically as a dependency
        rCode = StartDriver(schSCManager, DRIVER_NAME);
        break;

    case TRUE:
        // Stop the driver.
        StopDriver(schSCManager, DRIVER_NAME);
        StopDriver(schSCManager, DRIVER2_NAME);

        // Remove the driver service.
        RemoveDriver(schSCManager, DRIVER_NAME);
        RemoveDriver(schSCManager, DRIVER2_NAME);

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
