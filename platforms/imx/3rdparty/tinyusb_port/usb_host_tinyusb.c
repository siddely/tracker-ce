/*
 * TinyUSB host glue for Tracker+ / Tracker Mini USB host mode. See usb/usb_host_mode.h.
 *
 * Kept separate from the NXP USB files on purpose: the two stacks' headers are never
 * included in the same translation unit.
 */
#include "tusb.h"
#include "host/hcd.h"

#include "fsl_device_registers.h"
#include "usb_host_log.h"
#include "usb_host_mode.h"

#include <stdio.h>
#include <string.h>

extern uint32_t platformMillis(void);
static void updateDeviceNames(void);
static void retryStuckEnumeration(void);

//--------------------------------------------------------------------+
// OSAL time source (required by TinyUSB with OPT_OS_NONE)
//--------------------------------------------------------------------+
uint32_t tusb_time_millis_api(void)
{
    return platformMillis();
}

//--------------------------------------------------------------------+
// Outgoing MIDI ring: filled from any context, drained in USB_HostStackTask()
//--------------------------------------------------------------------+
#define HOST_TX_RING_SIZE 128U

static volatile uint32_t s_txRing[HOST_TX_RING_SIZE];
static volatile uint32_t s_txHead = 0;
static volatile uint32_t s_txTail = 0;

static volatile uint8_t s_mountedMask = 0;

void USB_HostMidiTx(uint32_t packet)
{
    if (s_mountedMask == 0U)
    {
        return; // nothing to send to; don't let stale packets build up
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t next = (s_txHead + 1U) % HOST_TX_RING_SIZE;
    if (next != s_txTail)
    {
        s_txRing[s_txHead] = packet;
        s_txHead = next;
    }
    // ring full: drop the packet rather than block the caller

    __set_PRIMASK(primask);
}

static bool txPop(uint32_t* packet)
{
    bool ok = false;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (s_txTail != s_txHead)
    {
        *packet = s_txRing[s_txTail];
        s_txTail = (s_txTail + 1U) % HOST_TX_RING_SIZE;
        ok = true;
    }

    __set_PRIMASK(primask);
    return ok;
}

static void txClear(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_txHead = 0;
    s_txTail = 0;
    __set_PRIMASK(primask);
}

//--------------------------------------------------------------------+
// Connected device names, for the UI
//--------------------------------------------------------------------+
// A MIDI interface is listed only once its product name has been read (or that failed), so
// the UI sees each device appear once, with its real name.
#define NAME_LEN 32U

typedef struct
{
    uint8_t daddr;
    bool listed;
    bool nameRequested;
    uint32_t mountedAt;
    char name[NAME_LEN];
} host_midi_info_t;

// How long to wait for a device's product name before listing it by VID:PID instead
#define NAME_TIMEOUT_MS 1500U

static host_midi_info_t s_info[CFG_TUH_MIDI];
static volatile uint16_t s_listVersion = 0;

// UTF-16LE string descriptor buffer, read by USB DMA (lives in non-cached DTCM)
CFG_TUH_MEM_ALIGN static uint16_t s_strDesc[CFG_TUH_MIDI][NAME_LEN + 1];

uint8_t USB_HostMidiDeviceCount(void)
{
    uint8_t count = 0;
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++)
    {
        if (s_info[idx].listed)
            count++;
    }
    return count;
}

const char* USB_HostMidiDeviceName(uint8_t n)
{
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++)
    {
        if (s_info[idx].listed)
        {
            if (n == 0)
                return s_info[idx].name;
            n--;
        }
    }
    return NULL;
}

uint16_t USB_HostMidiListVersion(void)
{
    return s_listVersion;
}

// Raw port state for diagnosing connection problems, e.g. "RUN H SE0 PP VBUS sc=1000"
const char* USB_HostPortDebugText(void)
{
    static char text[48];
    static const char* const kLineState[4] = { "SE0", "K", "J", "SE1" };

    uint32_t portsc = USB1->PORTSC1;
    uint32_t cmd = USB1->USBCMD;
    uint32_t mode = USB1->USBMODE & USBHS_USBMODE_CM_MASK;
    bool vbus = (USB_ANALOG->INSTANCE[0].VBUS_DETECT_STAT &
                 USB_ANALOG_VBUS_DETECT_STAT_VBUS_VALID_MASK) != 0U;

    snprintf(text,
             sizeof(text),
             "%s %s %s%s%s%s sc=%08lX",
             (cmd & USBHS_USBCMD_RS_MASK) ? "RUN" : "STOP",
             mode == 3U ? "H" : (mode == 2U ? "D" : "?"),
             kLineState[(portsc & USBHS_PORTSC1_LS_MASK) >> USBHS_PORTSC1_LS_SHIFT],
             (portsc & USBHS_PORTSC1_PP_MASK) ? " PP" : "",
             vbus ? " VBUS" : "",
             (portsc & USBHS_PORTSC1_PHCD_MASK) ? " PHCD" : "",
             (unsigned long)portsc);
    return text;
}

// e.g. "FS device, 1 USB / 1 MIDI" or "Nothing on port"
const char* USB_HostStatusText(void)
{
    static char text[40];

    if (!USB_HostModeIsActive())
    {
        return USB_HostModeState() == USB_HOST_STATE_HOST_FAILED ? "Host failed to start"
                                                                 : "Device mode";
    }

    // The Tracker can't supply 5 V itself, so whether something else does is the first thing
    // to know when nothing connects.
    bool vbus = (USB_ANALOG->INSTANCE[0].VBUS_DETECT_STAT &
                 USB_ANALOG_VBUS_DETECT_STAT_VBUS_VALID_MASK) != 0U;

    uint32_t portsc = USB1->PORTSC1;
    if ((portsc & USBHS_PORTSC1_CCS_MASK) == 0U)
    {
        return vbus ? "Host, 5V ok, nothing on port" : "Host, no 5V, nothing on port";
    }

    static const char* const kSpeeds[4] = { "FS", "LS", "HS", "?" };
    const char* speed = kSpeeds[(portsc & USBHS_PORTSC1_PSPD_MASK) >> USBHS_PORTSC1_PSPD_SHIFT];

    uint8_t usbCount = 0;
    for (uint8_t daddr = 1; daddr <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; daddr++)
    {
        if (tuh_mounted(daddr))
            usbCount++;
    }

    snprintf(text,
             sizeof(text),
             "Host, %s device, %u USB / %u MIDI",
             speed,
             usbCount,
             USB_HostMidiDeviceCount());
    return text;
}

static void listDevice(uint8_t idx)
{
    if (s_info[idx].name[0] == '\0')
    {
        uint16_t vid = 0, pid = 0;
        (void)tuh_vid_pid_get(s_info[idx].daddr, &vid, &pid);
        snprintf(s_info[idx].name, NAME_LEN, "USB MIDI %04X:%04X", vid, pid);
    }
    s_info[idx].listed = true;
    s_listVersion++;
    usbLog("MIDI %u listed as \"%s\"", idx, s_info[idx].name);
}

static void productStringCb(tuh_xfer_t* xfer)
{
    uint8_t idx = (uint8_t)xfer->user_data;
    if (idx >= CFG_TUH_MIDI || !tuh_midi_mounted(idx) || s_info[idx].listed)
        return; // gone, or already listed after a timeout

    s_info[idx].name[0] = '\0';
    usbLog("MIDI %u name request: result %u, %lu bytes",
           idx,
           (unsigned)xfer->result,
           (unsigned long)xfer->actual_len);
    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 2U)
    {
        // String descriptor: bLength, bDescriptorType, then UTF-16LE. Keep printable ASCII.
        const uint8_t* desc = (const uint8_t*)s_strDesc[idx];
        uint32_t len = desc[0];
        if (len > xfer->actual_len)
            len = xfer->actual_len;

        uint32_t out = 0;
        for (uint32_t i = 2; i + 1 < len && out < NAME_LEN - 1U; i += 2)
        {
            uint16_t c = (uint16_t)(desc[i] | (desc[i + 1] << 8));
            s_info[idx].name[out++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        while (out > 0 && s_info[idx].name[out - 1] == ' ')
            out--;
        s_info[idx].name[out] = '\0';
    }
    listDevice(idx);
}

//--------------------------------------------------------------------+
// PHY handling
//--------------------------------------------------------------------+
static bool s_hsDisconnectDetect = false;

static void phyHostSetup(void)
{
    // Allow low/full speed devices, including through a high speed hub
    USBPHY1->CTRL_SET = USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;
    USBPHY1->PWD = 0;
    s_hsDisconnectDetect = false;
}

static void phyHostTeardown(void)
{
    USBPHY1->CTRL_CLR = USBPHY_CTRL_CLR_ENHOSTDISCONDETECT_MASK;
    s_hsDisconnectDetect = false;
}

// The i.MX RT PHY only detects unplugging of a high speed device when ENHOSTDISCONDETECT
// is set, and that bit must only be set while a high speed device is connected and enabled.
static void phyUpdateDisconnectDetect(void)
{
    uint32_t portsc = USB1->PORTSC1;
    bool connected = (portsc & USBHS_PORTSC1_CCS_MASK) != 0U;
    bool enabled = (portsc & USBHS_PORTSC1_PE_MASK) != 0U;
    bool highSpeed = ((portsc & USBHS_PORTSC1_PSPD_MASK) >> USBHS_PORTSC1_PSPD_SHIFT) == 2U;
    bool want = connected && enabled && highSpeed;

    if (want != s_hsDisconnectDetect)
    {
        if (want)
        {
            USBPHY1->CTRL_SET = USBPHY_CTRL_SET_ENHOSTDISCONDETECT_MASK;
        }
        else
        {
            USBPHY1->CTRL_CLR = USBPHY_CTRL_CLR_ENHOSTDISCONDETECT_MASK;
        }
        s_hsDisconnectDetect = want;
    }
}

//--------------------------------------------------------------------+
// Verbose log: port state, USB errors, MIDI traffic
//--------------------------------------------------------------------+
static volatile uint32_t s_irqCount = 0;
static volatile uint32_t s_errIrqCount = 0;    // USBSTS.UEI: a transfer ended with an error
static volatile uint32_t s_systemErrCount = 0; // USBSTS.SEI: the controller's DMA failed
static uint32_t s_portWatch = 0xFFFFFFFFU;
static int s_vbusWatch = -1;      // last 5 V state written to the log
static int s_vbusRaw = -1;        // last 5 V state seen
static uint32_t s_vbusRawSince = 0;
static uint32_t s_vbusFlickers = 0;

// 5 V can bounce very fast (USB-C power negotiation, a loose plug); log it only once it has
// been steady this long, with how many times it changed in between.
#define VBUS_STEADY_MS 300U
static uint32_t s_loggedErr = 0;
static uint32_t s_loggedSysErr = 0;

static uint32_t s_rxPackets = 0;
static uint32_t s_txPackets = 0;
static uint32_t s_txDropped = 0;
static uint32_t s_rxLoggedThisMount = 0;
static uint32_t s_lastStatsAt = 0;
static uint32_t s_statsRx = 0, s_statsTx = 0, s_statsDrop = 0;

#define RX_PACKETS_LOGGED_PER_MOUNT 16U
#define STATS_INTERVAL_MS           2000U

static void logPortChanges(void)
{
    const uint32_t watched = USBHS_PORTSC1_CCS_MASK | USBHS_PORTSC1_PE_MASK |
                             USBHS_PORTSC1_PR_MASK | USBHS_PORTSC1_SUSP_MASK |
                             USBHS_PORTSC1_PP_MASK | USBHS_PORTSC1_OCA_MASK |
                             USBHS_PORTSC1_PSPD_MASK;
    uint32_t portsc = USB1->PORTSC1;
    uint32_t state = portsc & watched;

    if (state != s_portWatch)
    {
        static const char* const kSpeeds[4] = { "FS", "LS", "HS", "?" };
        usbLog("Port: %s%s%s%s%s%s %s (PORTSC %08lX)",
               (portsc & USBHS_PORTSC1_CCS_MASK) ? "connected" : "empty",
               (portsc & USBHS_PORTSC1_PE_MASK) ? ", enabled" : "",
               (portsc & USBHS_PORTSC1_PR_MASK) ? ", in reset" : "",
               (portsc & USBHS_PORTSC1_SUSP_MASK) ? ", suspended" : "",
               (portsc & USBHS_PORTSC1_PP_MASK) ? "" : ", power off",
               (portsc & USBHS_PORTSC1_OCA_MASK) ? ", OVERCURRENT" : "",
               kSpeeds[(portsc & USBHS_PORTSC1_PSPD_MASK) >> USBHS_PORTSC1_PSPD_SHIFT],
               (unsigned long)portsc);
        s_portWatch = state;
    }

    int vbus = (USB_ANALOG->INSTANCE[0].VBUS_DETECT_STAT &
                USB_ANALOG_VBUS_DETECT_STAT_VBUS_VALID_MASK) != 0U;
    uint32_t now = platformMillis();
    if (vbus != s_vbusRaw)
    {
        if (s_vbusRaw >= 0)
            s_vbusFlickers++;
        s_vbusRaw = vbus;
        s_vbusRawSince = now;
    }
    else if (vbus != s_vbusWatch && now - s_vbusRawSince >= VBUS_STEADY_MS)
    {
        if (s_vbusFlickers > 1U)
            usbLog("5V on port: %s (after %lu changes)",
                   vbus ? "yes" : "NO",
                   (unsigned long)s_vbusFlickers);
        else
            usbLog("5V on port: %s", vbus ? "yes" : "NO");
        s_vbusWatch = vbus;
        s_vbusFlickers = 0;
    }

    if (s_errIrqCount != s_loggedErr)
    {
        usbLog("USB transfer errors: %lu (of %lu irqs)",
               (unsigned long)s_errIrqCount,
               (unsigned long)s_irqCount);
        s_loggedErr = s_errIrqCount;
    }
    if (s_systemErrCount != s_loggedSysErr)
    {
        usbLog("USB SYSTEM ERROR (DMA) x%lu: controller halted?", (unsigned long)s_systemErrCount);
        s_loggedSysErr = s_systemErrCount;
    }
}

static void logMidiStats(void)
{
    uint32_t now = platformMillis();
    if (now - s_lastStatsAt < STATS_INTERVAL_MS)
        return;
    s_lastStatsAt = now;

    if (s_rxPackets != s_statsRx || s_txPackets != s_statsTx || s_txDropped != s_statsDrop)
    {
        usbLog("MIDI totals: in %lu, out %lu, out failed %lu",
               (unsigned long)s_rxPackets,
               (unsigned long)s_txPackets,
               (unsigned long)s_txDropped);
        s_statsRx = s_rxPackets;
        s_statsTx = s_txPackets;
        s_statsDrop = s_txDropped;
    }
}

static void resetLogState(void)
{
    s_portWatch = 0xFFFFFFFFU;
    s_vbusWatch = -1;
    s_vbusRaw = -1;
    s_vbusFlickers = 0;
    s_irqCount = 0;
    s_errIrqCount = 0;
    s_systemErrCount = 0;
    s_loggedErr = 0;
    s_loggedSysErr = 0;
    s_rxPackets = s_txPackets = s_txDropped = 0;
    s_statsRx = s_statsTx = s_statsDrop = 0;
}

//--------------------------------------------------------------------+
// Enumeration retry
//--------------------------------------------------------------------+
// A hub or device that isn't ready the first time (still powering up, or a bus-powered
// device behind a hub whose power arrives late) can fail enumeration and then sit on the
// port forever, since nothing new happens on the line. If something has been connected for
// a while with nothing mounted, power the port off and on, which looks like a replug.
#define RETRY_AFTER_MS   3000U
#define RETRY_OFF_MS     200U
#define RETRY_MAX        5U

static uint32_t s_connectedSince = 0;
static uint32_t s_portOffSince = 0;
static bool s_portOff = false;
static uint8_t s_retries = 0;

static bool anythingMounted(void)
{
    for (uint8_t daddr = 1; daddr <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; daddr++)
    {
        if (tuh_mounted(daddr))
            return true;
    }
    return false;
}

static void retryStuckEnumeration(void)
{
    const uint32_t now = platformMillis();
    // PORTSC1 change bits are write-1-to-clear: never write them back
    const uint32_t w1c = USBHS_PORTSC1_CSC_MASK | USBHS_PORTSC1_PEC_MASK | USBHS_PORTSC1_OCC_MASK;

    if (s_portOff)
    {
        if (now - s_portOffSince >= RETRY_OFF_MS)
        {
            USB1->PORTSC1 = (USB1->PORTSC1 & ~w1c) | USBHS_PORTSC1_PP_MASK;
            s_portOff = false;
            s_connectedSince = now;
            usbLog("Retry: port power back on");
        }
        return;
    }

    const bool connected = (USB1->PORTSC1 & USBHS_PORTSC1_CCS_MASK) != 0U;
    if (!connected)
    {
        s_connectedSince = 0;
        s_retries = 0; // a real unplug: start counting again next time
        return;
    }

    if (anythingMounted())
    {
        s_connectedSince = now;
        s_retries = 0;
        return;
    }

    if (s_connectedSince == 0)
    {
        s_connectedSince = now;
        return;
    }

    if (now - s_connectedSince >= RETRY_AFTER_MS && s_retries < RETRY_MAX)
    {
        s_retries++;
        usbLog("Retry %u/%u: connected %lu ms, nothing mounted, port power off",
               s_retries,
               RETRY_MAX,
               (unsigned long)(now - s_connectedSince));
        USB1->PORTSC1 = USB1->PORTSC1 & ~(w1c | USBHS_PORTSC1_PP_MASK);
        s_portOff = true;
        s_portOffSince = now;
    }
}

bool USB_HostStackInit(void)
{
    s_connectedSince = 0;
    s_portOff = false;
    s_retries = 0;

    s_mountedMask = 0;
    memset(s_info, 0, sizeof(s_info));
    s_listVersion++;
    txClear();
    resetLogState();
    phyHostSetup();

    usbLog("Host stack init (PHY CTRL %08lX, PWD %08lX)",
           (unsigned long)USBPHY1->CTRL,
           (unsigned long)USBPHY1->PWD);

    const tusb_rhport_init_t hostInit = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    bool ok = tusb_init(BOARD_TUH_RHPORT, &hostInit);
    usbLog("Host stack init %s (USBCMD %08lX, USBMODE %08lX)",
           ok ? "OK" : "FAILED",
           (unsigned long)USB1->USBCMD,
           (unsigned long)USB1->USBMODE);
    return ok;
}

void USB_HostStackDeinit(void)
{
    usbLog("Host stack deinit");
    (void)tuh_deinit(BOARD_TUH_RHPORT);
    hcd_int_disable(BOARD_TUH_RHPORT);
    phyHostTeardown();
    s_mountedMask = 0;
    memset(s_info, 0, sizeof(s_info));
    s_listVersion++;
    txClear();
}

void USB_HostStackIrqHandler(void)
{
    uint32_t status = USB1->USBSTS & USB1->USBINTR;
    s_irqCount++;
    if (status & USBHS_USBSTS_UEI_MASK)
        s_errIrqCount++;
    if (status & USBHS_USBSTS_SEI_MASK)
        s_systemErrCount++;

    tusb_int_handler(BOARD_TUH_RHPORT, true);
}

void USB_HostStackTask(void)
{
    tuh_task();
    phyUpdateDisconnectDetect();
    logPortChanges();
    updateDeviceNames();
    retryStuckEnumeration();
    logMidiStats();

    if (s_mountedMask == 0U)
    {
        return;
    }

    // Incoming: every mounted MIDI interface feeds the same queue as USB MIDI from a computer
    static uint32_t rxBuf[16];
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++)
    {
        if ((s_mountedMask & (1U << idx)) == 0U)
        {
            continue;
        }

        uint32_t n;
        while ((n = tuh_midi_packet_read_n(idx, (uint8_t*)rxBuf, sizeof(rxBuf))) > 0U)
        {
            for (uint32_t i = 0; i < n / 4U; i++)
            {
                const uint8_t* b = (const uint8_t*)&rxBuf[i];
                s_rxPackets++;
                if (s_rxLoggedThisMount < RX_PACKETS_LOGGED_PER_MOUNT)
                {
                    s_rxLoggedThisMount++;
                    usbLog("MIDI %u in: %02X %02X %02X %02X", idx, b[0], b[1], b[2], b[3]);
                }
            }
            midiUsbParseRx((const uint8_t*)rxBuf, n);
        }
    }

    // Outgoing: send the Tracker's MIDI out to every mounted device (cable 0)
    uint32_t packet;
    bool wrote = false;
    while (txPop(&packet))
    {
        for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++)
        {
            if (s_mountedMask & (1U << idx))
            {
                if (tuh_midi_packet_write_n(idx, (const uint8_t*)&packet, 4) == 4U)
                    s_txPackets++;
                else
                    s_txDropped++;
            }
        }
        wrote = true;
    }

    if (wrote)
    {
        for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++)
        {
            if (s_mountedMask & (1U << idx))
            {
                (void)tuh_midi_write_flush(idx);
            }
        }
    }
}

//--------------------------------------------------------------------+
// TinyUSB MIDI host callbacks
//--------------------------------------------------------------------+
void tuh_mount_cb(uint8_t daddr)
{
    uint16_t vid = 0, pid = 0;
    (void)tuh_vid_pid_get(daddr, &vid, &pid);
    usbLog("Dev %u mounted: VID:PID %04X:%04X", daddr, vid, pid);
}

void tuh_umount_cb(uint8_t daddr)
{
    usbLog("Dev %u unmounted", daddr);
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* mount_cb_data)
{
    usbLog("MIDI %u mounted: dev %u, interface %u, %u in / %u out cables",
           idx,
           mount_cb_data->daddr,
           mount_cb_data->bInterfaceNumber,
           mount_cb_data->rx_cable_count,
           mount_cb_data->tx_cable_count);

    if (idx >= CFG_TUH_MIDI)
    {
        usbLog("MIDI %u ignored: only %u MIDI devices supported", idx, CFG_TUH_MIDI);
        return;
    }
    s_rxLoggedThisMount = 0;

    s_mountedMask |= (uint8_t)(1U << idx);

    // The name is requested from USB_HostStackTask() once enumeration has finished: this
    // callback runs in the middle of it, while the control endpoint is still busy.
    s_info[idx].daddr = mount_cb_data->daddr;
    s_info[idx].listed = false;
    s_info[idx].nameRequested = false;
    s_info[idx].mountedAt = platformMillis();
    s_info[idx].name[0] = '\0';
}

// Requests product names for newly mounted devices, and lists any device whose name hasn't
// arrived in time under its VID:PID, so every device shows up in the UI.
static void updateDeviceNames(void)
{
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++)
    {
        host_midi_info_t* info = &s_info[idx];
        if ((s_mountedMask & (1U << idx)) == 0U || info->listed)
            continue;

        if (!info->nameRequested && tuh_mounted(info->daddr))
        {
            info->nameRequested = tuh_descriptor_get_product_string(info->daddr,
                                                                    0x0409, // US English
                                                                    s_strDesc[idx],
                                                                    sizeof(s_strDesc[idx]),
                                                                    productStringCb,
                                                                    idx);
            usbLog("MIDI %u name requested from dev %u: %s",
                   idx,
                   info->daddr,
                   info->nameRequested ? "queued" : "refused (busy?)");
        }

        if (!info->listed && platformMillis() - info->mountedAt > NAME_TIMEOUT_MS)
        {
            usbLog("MIDI %u: no name after %u ms, using VID:PID", idx, NAME_TIMEOUT_MS);
            info->name[0] = '\0';
            listDevice(idx); // falls back to VID:PID
        }
    }
}

void tuh_midi_umount_cb(uint8_t idx)
{
    usbLog("MIDI %u unmounted", idx);
    if (idx >= CFG_TUH_MIDI)
        return;

    s_mountedMask &= (uint8_t)~(1U << idx);
    if (s_info[idx].listed)
    {
        s_info[idx].listed = false;
        s_listVersion++;
    }
}
