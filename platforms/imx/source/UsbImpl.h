#ifndef USBIMPL_H_
#define USBIMPL_H_

#include "Platform.h"
#include "composite.h"
#include "config.h"

extern "C" usb_composite_state_t g_UsbState;

class UsbImpl final : public Usb
{
  public:
    virtual void init() override;
    /**
     * This method should be called whenever any changes in enabled
     * functions are done using SetClassState method to reset USB
     * and allow enumeration of a new set of USB functions
     */
    virtual void reset(void) override;
    /**
     * For not critical USB endpoints this method should be called
     * on regular basis to handle them in non IRQ context i.e. storage and serial
     */
    virtual void update(void) override;
    /**
     * This method is responsible for enabling required USB functions.
     *
     * Midi - enabled by default and should be always enabled
     *
     * AudioIn and AudioOut - enabled by default but should be configurable
     * 	from UI and enabled based on NVM settings
     *
     * Storage - disabled by default unless develop mode is enabled. Should
     * 	be configurable from UI on demand
     *
     * Serial - disabled by default unless develop mode is enabled.
     */
    virtual void setClassState(Usb::ClassType type, bool enable) override;
    /**
     * This method retrieves whether certain USB function is enabled
     */
    virtual bool getClassState(Usb::ClassType type) override;
    /**
     * This method returns true if device is connected to a host and false when not
     */
    virtual bool isAttached() override;
    /**
     * Switches the USB-C port between device mode (to a computer) and host mode
     * (USB MIDI controllers plugged into the Tracker). Applied on the next update().
     */
    virtual bool hasHostMode() override { return true; }
    virtual void setHostMode(bool enable) override;
    virtual bool isHostMode() override;
    /**
     * Number of USB MIDI devices mounted while in host mode
     */
    virtual uint8_t getHostDeviceCount() override;
    virtual const char* getHostDeviceName(uint8_t n) override;
    virtual uint16_t getHostDeviceListVersion() override;
    virtual uint8_t getHostState() override;
    virtual const char* getHostStatus() override;
    virtual const char* getHostPortDebug() override;
    virtual uint32_t getLogFirst() override;
    virtual uint32_t getLogNext() override;
    virtual bool getLogLine(uint32_t n, char* out, uint32_t size) override;
    virtual void clearLog() override;
    virtual bool saveLog(const char* path) override;
};

#endif /* USBIMPL_H_ */
