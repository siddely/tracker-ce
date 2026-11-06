/*
 * USB host mode switching for Tracker+ / Tracker Mini. See usb_host_mode.h.
 *
 * Device -> host: tear down the NXP device stack completely (USB_DeviceClassDeinit), then
 * start the TinyUSB host stack on the same controller.
 * Host -> device: stop the TinyUSB host stack, then run the normal USB_Init() again.
 *
 * Everything here runs from the main loop. The USB interrupt is masked across each switch,
 * and USB_OTG1_IRQHandler (composite.c) routes to whichever stack currently owns the port.
 */
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"

#include "composite.h"
#include "fsl_device_registers.h"
#include "usb_host_mode.h"
#include "usb_host_log.h"

extern usb_device_composite_struct_t g_composite;
void USB_Init(void);
void USB_DeviceIsrDisable(void);

static volatile bool s_hostActive = false;
static volatile uint8_t s_hostState = USB_HOST_STATE_DEVICE;
static volatile bool s_hostRequested = false;
static volatile bool s_requestPending = false;

bool USB_HostModeIsActive(void)
{
    return s_hostActive;
}

uint8_t USB_HostModeState(void)
{
    return s_hostState;
}

void USB_HostModeRequest(bool enable)
{
    usbLog("Mode request: %s (now %s)", enable ? "host" : "device", s_hostActive ? "host" : "device");
    s_hostRequested = enable;
    s_requestPending = true;
}

static void clearDeviceState(void)
{
    g_composite.attach = 0U;
    g_composite.currentConfiguration = 0U;
    g_composite.cdcVcom.attach = 0U;
    g_composite.audioUnified.attach = 0U;
    g_composite.deviceHandle = NULL;
    g_composite.cdcVcom.cdcAcmHandle = (class_handle_t)NULL;
    g_composite.midi.midiHandle = (class_handle_t)NULL;
    g_composite.audioUnified.audioRecorderHandle = (class_handle_t)NULL;
    g_composite.audioUnified.audioPlayerHandle = (class_handle_t)NULL;
    g_composite.mscDisk.mscHandle = (class_handle_t)NULL;
}

static void enterHostMode(void)
{
    usbLog("Switching to host: stopping device stack");
    USB_DeviceIsrDisable();

    /* Stops and resets the controller, frees all device class handles. */
    (void)USB_DeviceClassDeinit(CONTROLLER_ID);
    clearDeviceState();
    midiUsbResetTx();

    s_hostActive = true;

    if (USB_HostStackInit())
    {
        s_hostState = USB_HOST_STATE_HOST;
    }
    else
    {
        /* Could not start the host stack: fall back to normal device mode. */
        s_hostActive = false;
        s_hostState = USB_HOST_STATE_HOST_FAILED;
        usbLog("Host start FAILED, back to device mode");
        USB_Init();
    }
}

static void enterDeviceMode(void)
{
    usbLog("Switching to device: stopping host stack");
    USB_DeviceIsrDisable();

    USB_HostStackDeinit();
    s_hostActive = false;
    s_hostState = USB_HOST_STATE_DEVICE;

    midiUsbResetTx();
    USB_Init();
}

void USB_HostModeService(void)
{
    if (!s_requestPending)
    {
        return;
    }
    s_requestPending = false;

    if (s_hostRequested && !s_hostActive)
    {
        enterHostMode();
    }
    else if (!s_hostRequested && s_hostState == USB_HOST_STATE_HOST_FAILED)
    {
        s_hostState = USB_HOST_STATE_DEVICE;
    }
    else if (!s_hostRequested && s_hostActive)
    {
        enterDeviceMode();
    }
}
