/*
 * TinyUSB configuration for USB MIDI host mode on Tracker+ / Tracker Mini (i.MX RT1051).
 *
 * Only the host stack is built. The device side stays on the NXP USB stack, and the two
 * never run at the same time: see usb/usb_host_mode.c.
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define CFG_TUSB_MCU OPT_MCU_MIMXRT1XXX
#define CFG_TUSB_OS  OPT_OS_NONE

// Verbose: TinyUSB's enumeration steps and failed requests go to the USB log
// (Config > USB > Log, usb/usb_host_log.c). Level 2 = enumeration detail, one line per transfer.
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 2
#endif
#define CFG_TUSB_DEBUG_PRINTF usbLogPrintf

// Host only, on the single USB-C port (USB_OTG1, rhport 0)
#define CFG_TUD_ENABLED    0
#define CFG_TUH_ENABLED    1
#define CFG_TUH_RPI_PIO_USB 0
#define BOARD_TUH_RHPORT   0
#define CFG_TUH_MAX_SPEED  OPT_MODE_HIGH_SPEED

// USB buffers live in DTCM (default .bss on this target), which is not cached and is
// reachable by the USB DMA. TinyUSB skips cache maintenance for DTCM addresses, so leaving
// this on only matters if a buffer ever lands in cached memory.
#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN            __attribute__((aligned(32)))
#define CFG_TUH_MEM_DCACHE_ENABLE    1
#define CFG_TUH_MEM_DCACHE_LINE_SIZE 32

// Must hold a device's whole configuration descriptor. Composite devices with USB audio
// (e.g. grooveboxes that are also audio interfaces) easily exceed 256 bytes, and enumeration
// fails outright if it doesn't fit.
#define CFG_TUH_ENUMERATION_BUFSIZE 1024

// DTCM is nearly full on this target, so keep the host stack small, but big enough for
// USB-C docks and adapters: these often contain two hubs plus a "Billboard" device of their
// own (e.g. VIA Labs hub + 1A40:0801 hub + Chrontel CH7210), before anything is plugged in.
#define CFG_TUH_HUB          2
#define CFG_TUH_DEVICE_MAX   6
// This is the highest endpoint *number* + 1, not a count: TinyUSB indexes per-device tables by
// endpoint number, and composite devices often put their MIDI endpoints above 3.
#define CFG_TUH_ENDPOINT_MAX 16
// The EHCI driver would otherwise size its endpoint pool as DEVICE_MAX * ENDPOINT_MAX.
// Control endpoints have their own slots, so this only needs the MIDI bulk endpoints
// (2 per MIDI interface) plus the hub's interrupt endpoint, with headroom.
#define CFG_TUH_EHCI_QHD_MAX 10

#define CFG_TUH_MIDI            2
#define CFG_TUH_MIDI_STREAM_API 0
// Endpoint buffer stays at 512 so high speed MIDI devices work; the FIFOs can be small
// because the main loop drains them continuously.
#define CFG_TUH_MIDI_EP_BUFSIZE 512
#define CFG_TUH_MIDI_RX_BUFSIZE 128
#define CFG_TUH_MIDI_TX_BUFSIZE 128

// Unused classes
#define CFG_TUH_HID    0
#define CFG_TUH_MSC    0
#define CFG_TUH_CDC    0
#define CFG_TUH_VENDOR 0
#define CFG_TUH_MIDI2  0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
