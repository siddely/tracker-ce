#include <UsbImpl.h>
#include "usb_host_mode.h"
#include "usb_host_log.h"
#include "SD.h"

extern "C" void USB_Init();
extern "C" void USB_Task();
extern "C" void USB_DeviceReset();
extern "C" uint8_t USB_DeviceVcomClear();

void UsbImpl::init()
{
    usbLog("Device stack init (boot)");
    USB_Init();
}

void UsbImpl::reset(void)
{
    g_UsbState.isResetRequested = true;
}

void UsbImpl::update(void)
{
    USB_Task();

    if (USB_HostModeIsActive())
    {
        // Device class changes (audio, storage) take effect when device mode is restored,
        // since USB_Init() rebuilds the descriptors from g_UsbState.
        g_UsbState.isResetRequested = false;
        return;
    }

    if (g_UsbState.isResetRequested)
    {
        // we want VCOM to be disconnected
        if (USB_DeviceVcomClear() != 0)
        {
            g_UsbState.isResetRequested = false;
            USB_DeviceReset();
        }
    }
}

void UsbImpl::setClassState(Usb::ClassType type, bool enable)
{
    switch (type)
    {
        case Usb::ClassType::kMidi:
            g_UsbState.isMidiEnabled = enable;
            break;
        case Usb::ClassType::kAudioIn:
            g_UsbState.isAudioInEnabled = enable;
            break;
        case Usb::ClassType::kAudioOut:
            g_UsbState.isAudioOutEnabled = enable;
            break;
        case Usb::ClassType::kStorage:
            g_UsbState.isStorageEnabled = enable;
            break;
        case Usb::ClassType::kSerial:
            g_UsbState.isSerialEnabled = enable;
            break;
    }
}

bool UsbImpl::getClassState(Usb::ClassType type)
{
    bool ret = false;

    switch (type)
    {
        case Usb::ClassType::kMidi:
            ret = g_UsbState.isMidiEnabled;
            break;
        case Usb::ClassType::kAudioIn:
            ret = g_UsbState.isAudioInEnabled;
            break;
        case Usb::ClassType::kAudioOut:
            ret = g_UsbState.isAudioOutEnabled;
            break;
        case Usb::ClassType::kStorage:
            ret = g_UsbState.isStorageEnabled;
            break;
        case Usb::ClassType::kSerial:
            ret = g_UsbState.isSerialEnabled;
            break;
    }

    return ret;
}

extern "C" usb_device_composite_struct_t g_composite;

bool UsbImpl::isAttached()
{
    return g_composite.attach;
}

void UsbImpl::setHostMode(bool enable)
{
    usbLog("Config: Port mode = %s", enable ? "Host" : "Device");
    USB_HostModeRequest(enable);
}

uint32_t UsbImpl::getLogFirst()
{
    return usbLogFirst();
}

uint32_t UsbImpl::getLogNext()
{
    return usbLogNext();
}

bool UsbImpl::getLogLine(uint32_t n, char* out, uint32_t size)
{
    return usbLogGet(n, out, size);
}

void UsbImpl::clearLog()
{
    usbLogClear();
    usbLog("Log cleared");
}

bool UsbImpl::saveLog(const char* path)
{
    SdFile file;
    if (!file.open(path, SD_FILE_WRITE))
    {
        usbLog("Save to %s: could not open file", path);
        return false;
    }

    char line[USB_LOG_LINE_LEN + 2];
    for (uint32_t n = usbLogFirst(); n < usbLogNext(); n++)
    {
        if (usbLogGet(n, line, USB_LOG_LINE_LEN))
        {
            strcat(line, "\r\n");
            file.write(line, strlen(line));
        }
    }
    file.close();
    usbLog("Saved to %s", path);
    return true;
}

bool UsbImpl::isHostMode()
{
    return USB_HostModeIsActive();
}

uint8_t UsbImpl::getHostDeviceCount()
{
    return USB_HostMidiDeviceCount();
}

const char* UsbImpl::getHostDeviceName(uint8_t n)
{
    return USB_HostMidiDeviceName(n);
}

uint16_t UsbImpl::getHostDeviceListVersion()
{
    return USB_HostMidiListVersion();
}

uint8_t UsbImpl::getHostState()
{
    return USB_HostModeState();
}

const char* UsbImpl::getHostStatus()
{
    return USB_HostStatusText();
}

const char* UsbImpl::getHostPortDebug()
{
    return USB_HostPortDebugText();
}
