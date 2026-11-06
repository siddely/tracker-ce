/*
 * USB log ring buffer. See usb_host_log.h.
 */
#include "usb_host_log.h"

#include "fsl_device_registers.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern uint32_t platformMillis(void);

#define LOG_LINES 100U
#define STAMP_LEN 8U /* "123.456 " */
#define TEXT_MAX  (USB_LOG_LINE_LEN - STAMP_LEN - 1U)

/* 6.4 KB: kept in on-chip OCRAM, since DTCM and SDRAM are nearly full. Zeroed at startup. */
__attribute__((section(".bss.$SRAM_OC"))) static char s_lines[LOG_LINES][USB_LOG_LINE_LEN];

static volatile uint32_t s_next = 0;
static char s_pending[TEXT_MAX + 1];
static uint32_t s_pendingLen = 0;
static uint32_t s_pendingTime = 0;

static void commitPending(void)
{
    char* line = s_lines[s_next % LOG_LINES];
    uint32_t t = s_pendingTime;
    s_pending[s_pendingLen] = '\0';
    snprintf(line,
             USB_LOG_LINE_LEN,
             "%3lu.%03lu %s",
             (unsigned long)((t / 1000U) % 1000U),
             (unsigned long)(t % 1000U),
             s_pending);
    s_next++;
    s_pendingLen = 0;
}

static void append(const char* text)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (; *text != '\0'; text++)
    {
        char c = *text;
        if (c == '\r')
            continue;

        if (c == '\n')
        {
            if (s_pendingLen > 0U)
                commitPending();
            continue;
        }

        if (s_pendingLen == 0U)
            s_pendingTime = platformMillis();

        s_pending[s_pendingLen++] = (c >= 0x20 && c < 0x7F) ? c : '?';
        if (s_pendingLen >= TEXT_MAX)
            commitPending(); /* wrap long lines */
    }

    __set_PRIMASK(primask);
}

static bool inInterrupt(void)
{
    return __get_IPSR() != 0U;
}

int usbLogPrintf(const char* format, ...)
{
    if (inInterrupt())
        return 0;

    char buf[160];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    append(buf);
    return n;
}

void usbLog(const char* format, ...)
{
    if (inInterrupt())
        return;

    char buf[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf) - 1U, format, args);
    va_end(args);

    /* finish any partial TinyUSB line first, so events always start a line */
    append("\n");
    append(buf);
    append("\n");
}

uint32_t usbLogNext(void)
{
    return s_next;
}

uint32_t usbLogFirst(void)
{
    uint32_t next = s_next;
    return next > LOG_LINES ? next - LOG_LINES : 0U;
}

bool usbLogGet(uint32_t n, char* out, uint32_t size)
{
    bool ok = false;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t next = s_next;
    uint32_t first = next > LOG_LINES ? next - LOG_LINES : 0U;
    if (n >= first && n < next && size > 0U)
    {
        strncpy(out, s_lines[n % LOG_LINES], size - 1U);
        out[size - 1U] = '\0';
        ok = true;
    }

    __set_PRIMASK(primask);
    return ok;
}

void usbLogClear(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_next = 0;
    s_pendingLen = 0;
    __set_PRIMASK(primask);
}
