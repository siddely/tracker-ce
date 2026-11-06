# USB MIDI Host Mode (Tracker Mini and Tracker+)

Plug class-compliant USB MIDI keyboards, pad controllers and synths straight into the
Tracker's USB-C port, with no computer, phone or host box in between.

## Using it

1. **Config > USB > Port mode** and pick **Host (MIDI devices)**. The setting is saved and
   comes back after a restart.
2. Connect the MIDI device with a **powered** USB-C OTG Y-cable or a powered USB hub (see
   Power below).
3. Notes, CCs, pitch bend and clock from the device arrive exactly like USB MIDI from a
   computer, so everything set to listen to USB MIDI works as usual (note entry, live play,
   MIDI CC control, clock sync).
4. The Tracker's own USB MIDI output (notes, CCs, clock) goes to the connected devices,
   so you can also sequence a USB synth.

Set **Port mode** back to **Device (computer)** to use USB audio, USB MIDI to a computer or
USB storage again. Storage is blocked with a message while in Host mode.

Up to two MIDI devices are supported at once, through one hub. Multi-port devices are merged
into one input; the cable number is ignored, as in device mode.

When a device connects or disconnects, its name shows briefly on screen (its product name, or its USB VID:PID if it reports none within 1.5 s). **Config > USB >
Device 1 / Device 2** lists what is connected.

## Devices that are also hosts (Ableton Move, phones, other grooveboxes)

USB-C decides who is host from resistors on the cable's CC pins, not from software. The
Tracker's USB-C socket identifies it as a device, and firmware cannot change that. A device
that can also be a host (a dual-role device) therefore sees the Tracker as a device and
becomes the host itself when the two are connected directly with a C-to-C cable. Nothing
talks, since both are then hosts.

To make such a device act as a device, give it what a computer would:

- connect it through the powered hub or OTG adapter, using a **USB-A to USB-C** cable from
  the hub's USB-A port to the device. A USB-A to C cable identifies its C end as connected
  to a host, and the hub supplies the 5 V that many devices wait for before they turn on
  their USB.

Some devices, like phones, let you pick their USB role in their settings instead.

## Power

The Tracker cannot supply 5 V on its USB-C port. The connected device must be powered from
somewhere else:

- a USB-C OTG Y-cable with a power input (power bank or charger on one leg, MIDI device on the
  other), or
- a powered USB hub, connected to the Tracker with a USB-C to USB-A OTG adapter.

A plain OTG adapter on its own will not work: nothing powers the MIDI device.

## How it works

- Tracker+ and Tracker Mini use one USB controller (USB_OTG1 on the i.MX RT1051) for the
  USB-C port. It can run as device or host, but not both at once.
- Device mode stays on the existing NXP USB stack. Host mode uses the TinyUSB host stack
  (`platforms/imx/3rdparty/tinyusb`, MIT licence, v0.21.0 subset), with its USB MIDI host
  class.
- Switching (`platforms/imx/usb/usb_host_mode.c`) fully de-initialises one stack before
  starting the other. The USB interrupt (`USB_OTG1_IRQHandler` in `composite.c`) and
  `USB_Task()` are routed to whichever stack owns the port.
- Incoming packets go through `midiUsbParseRx()` in `usb_midi.c`, the same parser and queue
  the device stack uses, so the rest of the firmware sees no difference.
- Outgoing packets from `midiUsbTx()` go into a small ring that the main loop drains to every
  mounted device. Clock to a USB synth therefore has up to one main-loop pass of jitter,
  unlike device mode, which sends from the calling context.
- The PHY's high speed disconnect detection (`ENHOSTDISCONDETECT`) is enabled only while a
  high speed device is connected, as the i.MX RT reference manual requires.

## MIDI monitor

**Config > MIDI > MIDI monitor** shows incoming MIDI briefly on screen: note on/off with note
name, velocity and channel, CC number and value, transport and song position, plus where it
came from (Jack, USB, or the connected host-mode device's name when only one is connected).
MIDI clock and active sensing are left out so they don't flood the display. It shows what
reaches the Tracker's MIDI handling, so message types the Tracker ignores (pitch bend,
program change, aftertouch) don't appear.

## Memory

Host mode uses about 12.5 KB of DTCM, which is nearly full, for two hubs and two MIDI devices
(`3rdparty/tinyusb/tusb_config.h`). The USB log's 6.4 KB ring lives in OCRAM. The enumeration buffer (1 KB) and endpoint number
limit (16) are set so composite devices with USB audio enumerate; the EHCI endpoint pool is
capped separately (`CFG_TUH_EHCI_QHD_MAX`, a small local change to the vendored `ehci.c`).
Raise `CFG_TUH_MIDI` and `CFG_TUH_DEVICE_MAX` only if the build still leaves DTCM headroom.

## Status

Tested on a Tracker Mini: an AKT-0.1 (TinyUSB CDC + MIDI, full speed) through a RayCue USB-C
dock (VIA Labs hub, a second 1A40:0801 hub and a Chrontel billboard device), with MIDI in and
device names working, plus a phone connected in device mode. Builds for Tracker Mini, Tracker+
and the Original Tracker (where the option is not offered). Tracker+ is untested on hardware.

Not tested yet: devices that are also hosts (e.g. Ableton Move) through a USB-A to USB-C cable,
and how the Mini's charging behaves with 5 V coming from the dock while in host mode. If a
device doesn't connect, Config > USB > Log > Save writes `/USB_log.txt`, which shows where it
stopped.
