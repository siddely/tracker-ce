#include "mtMidiMonitor.h"

#include "core/interface.h"
#include "core/interfacePopups.h"
#include "mtStructs.h"

#include <elapsedMillis.h>
#include <stdio.h>

namespace
{
constexpr uint32_t kMinRefreshMs = 120; // keep the display responsive while playing fast

struct LoggedMessage
{
    MidiMonitorSource source;
    uint8_t status;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
};

LoggedMessage latest[2];
uint8_t loggedCount = 0;
bool pending = false;
elapsedMillis sinceShown;

char lines[2][48];

const char* sourceName(MidiMonitorSource source)
{
    if (source == MidiMonitorSource::kJack)
    {
        return "Jack";
    }

    Usb* usb = mtInterface.getPlatform()->getUsb();
    if (usb->isHostMode())
    {
        // Packets from several host-mode devices are merged, so only name a single one
        const char* name = usb->getHostDeviceCount() == 1 ? usb->getHostDeviceName(0) : nullptr;
        return name != nullptr ? name : "USB host";
    }
    return "USB";
}

void format(const LoggedMessage& m, char* out, size_t size)
{
    const char* src = sourceName(m.source);

    switch (m.status)
    {
        case 0x90:
            snprintf(out, size, "%s  Note on %s  vel %u  ch %u", src, mtNotes[m.data1 & 0x7F],
                     m.data2, m.channel);
            break;
        case 0x80:
            snprintf(out, size, "%s  Note off %s  ch %u", src, mtNotes[m.data1 & 0x7F], m.channel);
            break;
        case 0xB0:
            snprintf(out, size, "%s  CC %u = %u  ch %u", src, m.data1, m.data2, m.channel);
            break;
        case 0xF2:
            snprintf(out, size, "%s  Song position %u", src, m.data1 | (m.data2 << 7));
            break;
        case 0xFA: snprintf(out, size, "%s  Start", src); break;
        case 0xFB: snprintf(out, size, "%s  Continue", src); break;
        case 0xFC: snprintf(out, size, "%s  Stop", src); break;
        default: snprintf(out, size, "%s  Status %02X  %u %u", src, m.status, m.data1, m.data2);
    }
}
} // namespace

void midiMonitorLog(MidiMonitorSource source,
                    uint8_t status,
                    uint8_t channel,
                    uint8_t data1,
                    uint8_t data2)
{
    if (mtConfig.midi.monitor == 0 || status == 0xF8 || status == 0xFE)
    {
        return; // off, or clock / active sensing
    }

    latest[1] = latest[0];
    latest[0] = { source, status, channel, data1, data2 };
    if (loggedCount < 2)
    {
        loggedCount++;
    }
    pending = true;
}

void midiMonitorUpdate()
{
    if (!pending || sinceShown < kMinRefreshMs)
    {
        return;
    }

    if (mtConfig.midi.monitor == 0)
    {
        pending = false;
        loggedCount = 0;
        return;
    }

    pending = false;
    sinceShown = 0;

    format(latest[0], lines[0], sizeof(lines[0]));
    if (loggedCount > 1)
    {
        format(latest[1], lines[1], sizeof(lines[1]));
        mtPopups.show(0, lines[0], lines[1]);
    }
    else
    {
        mtPopups.show(0, lines[0]);
    }
}
