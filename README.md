# Process Monotoring driver

This is an educational task based on one of Microsoft's example drivers. Below is the original readme of the sample

## IOCTL

This sample demonstrates the usage of four different types of IOCTLs (METHOD\_IN\_DIRECT, METHOD\_OUT\_DIRECT, METHOD\_NEITHER, and METHOD\_BUFFERED).

The sample shows how the user input and output buffers specified in the **DeviceIoControl** function call are handled, in each case, by the I/O subsystem and the driver.

The sample consists of a legacy device driver and a Win32 console test application. The test application opens a handle to the device exposed by the driver and makes all four different **DeviceIoControl** calls, one after another. To understand how the IRP fields are set the I/O manager, you should run the checked build version of the driver and look at the debug output.

### Run the sample

To test this driver, copy the test app, Ioctlapp.exe, and the driver to the same directory, and run the application. The application will automatically load the driver, if it's not already loaded, and interact with the driver. When you exit the application, the driver will be stopped, unloaded and removed.
