/*
 * USB host mode for Tracker+ / Tracker Mini.
 *
 * The single USB-C port normally runs as a USB device (MIDI, audio, storage to a computer)
 * using the NXP stack. In host mode the same controller (USB_OTG1) is handed to the TinyUSB
 * host stack instead, so class-compliant USB MIDI controllers and synths can be plugged
 * straight into the Tracker. Only one mode runs at a time.
 *
 * Incoming MIDI from host-mode devices goes into the same queue as USB MIDI from a computer,
 * so everything that listens to "USB" MIDI input (notes, CC, clock) works unchanged.
 *
 * The Tracker cannot supply 5V on its USB-C port. The connected device must be powered
 * externally, e.g. with a powered OTG Y-cable or a powered hub.
 *
 * This header is deliberately free of NXP and TinyUSB includes so either side can use it.
 */
#ifndef USB_HOST_MODE_H_
#define USB_HOST_MODE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ---- Mode switching (usb_host_mode.c, call from the main loop only) ---- */

    /* Request host (true) or device (false) mode. Applied on the next USB_Task(). */
    void USB_HostModeRequest(bool enable);

    /* True while the controller is owned by the host stack. Safe from any context. */
    bool USB_HostModeIsActive(void);

#define USB_HOST_STATE_DEVICE      0U /* normal device mode */
#define USB_HOST_STATE_HOST        1U /* host stack running */
#define USB_HOST_STATE_HOST_FAILED 2U /* host requested but the stack didn't start */
    uint8_t USB_HostModeState(void);

    /* One-line description of the port in host mode, for the UI. */
    const char* USB_HostStatusText(void);
    /* Raw controller and port registers, summarised, for diagnosing connection problems. */
    const char* USB_HostPortDebugText(void);

    /* Applies a pending mode request. Called from USB_Task(). */
    void USB_HostModeService(void);

    /* USB MIDI devices connected in host mode, listed once their name is known. */
    uint8_t USB_HostMidiDeviceCount(void);
    /* Name of the n-th listed device (product string, or "USB MIDI VID:PID"), or NULL. */
    const char* USB_HostMidiDeviceName(uint8_t n);
    /* Changes whenever a device is listed or removed, or the mode changes. */
    uint16_t USB_HostMidiListVersion(void);

    /* Queue one USB-MIDI event packet for all mounted devices. Safe from any context. */
    void USB_HostMidiTx(uint32_t packet);

    /* ---- TinyUSB glue (3rdparty/tinyusb_port/usb_host_tinyusb.c) ---- */

    bool USB_HostStackInit(void);
    void USB_HostStackDeinit(void);
    void USB_HostStackTask(void);
    void USB_HostStackIrqHandler(void);

    /* ---- Shared with usb_midi.c ---- */

    /* Parse USB-MIDI event packets into the shared receive queue. */
    void midiUsbParseRx(const uint8_t* buf, uint32_t len);

    /* Clears device-side MIDI send buffers, used when returning to device mode. */
    void midiUsbResetTx(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HOST_MODE_H_ */
