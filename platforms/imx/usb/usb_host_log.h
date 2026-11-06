/*
 * USB log for Tracker+ / Tracker Mini: a ring of timestamped text lines describing what the
 * USB port is doing. Fed by the TinyUSB host stack's own debug output (enumeration steps,
 * failed requests) and by the Tracker's USB code (mode switches, port and 5 V changes, USB
 * errors, devices, MIDI traffic). Shown in Config > USB > Log and saved to /USB_log.txt.
 *
 * Plain C with no USB stack includes, so any file can log.
 */
#ifndef USB_HOST_LOG_H_
#define USB_HOST_LOG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Length of one stored line, including its timestamp and terminator */
#define USB_LOG_LINE_LEN 64U

    /* Log one event; a newline is added. Not from interrupts (ignored there). */
    void usbLog(const char* format, ...) __attribute__((format(printf, 1, 2)));

    /* printf-style sink for TinyUSB (CFG_TUSB_DEBUG_PRINTF): text is split into lines on '\n'. */
    int usbLogPrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));

    /* Lines are numbered from 0 since the last clear. Only the newest ones are kept. */
    uint32_t usbLogFirst(void); /* oldest line still stored */
    uint32_t usbLogNext(void);  /* number the next line will get */
    /* Copies line n into out; false if it is no longer stored. */
    bool usbLogGet(uint32_t n, char* out, uint32_t size);
    void usbLogClear(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HOST_LOG_H_ */
