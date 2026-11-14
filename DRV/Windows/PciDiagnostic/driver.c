/* SPDX-License-Identifier: BSD-3-Clause */
/* Windows driver author: Ahmad Byagowi */
#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD TimeCardPciDiagnosticAdd;

/* This diagnostic deliberately has no register, PCI configuration, interrupt,
 * DMA, child-device or user interface code. KMDF forwards the normal PnP and
 * power lifecycle to the PCI bus. It is not a functional Time Card driver. */
NTSTATUS
DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, TimeCardPciDiagnosticAdd);
    return WdfDriverCreate(driverObject, registryPath,
                           WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS
TimeCardPciDiagnosticAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit)
{
    WDFDEVICE device;
    UNREFERENCED_PARAMETER(driver);
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    return WdfDeviceCreate(&deviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
}
