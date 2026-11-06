
#include <string.h>

#include "configEditor/configEditor.h"
#include "configEditor/configMenu.h"

#include "fileManager.h"

#include "mtAudioEngine.h"
#include "mtPadBoard.h"

#include "Platform.h"
#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"

#include "Tracker.h"
#include "core/interface.h"
#include "core/interfacePopups.h"

// device startup actions after Config read
void initConfigValuesAfterReadConfig()
{
    usbAudioChange();
    usbHostChange();
    setDisplayBrightness();
}

// reset config actions
void restartConfigAllActions()
{
    usbAudioChange();
    usbHostChange();

    setRadioRegion();
    setDisplayBrightness();
    setPadsBrightness();

    setPadBoardValues();

    setAntialiasingEnable();

    setMetronomValues();
}

void setRadioRegion()
{
    if (mtConfig.general.radioRegion > 3)
        mtConfig.general.radioRegion = 0;

    mtInterface.getPlatform()->getRadio()->setRegion((Radio::Region)mtConfig.general.radioRegion);
}

void setDisplayBrightness()
{
    if (mtConfig.general.dispBrightness > 2)
        mtConfig.general.dispBrightness = 2;

    switch (mtConfig.general.dispBrightness)
    {
        case 0: display.setBacklightBrightness(20); break;
        case 1: display.setBacklightBrightness(50); break;
        case 2: display.setBacklightBrightness(128); break;
        default: break;
    }
}

void setDisplayTheme()
{
    if (mtConfig.general.theme >= ThemeManager::getThemeCount())
        mtConfig.general.theme = DisplayThemes::kOriginal;
    ThemeManager::applyTheme(mtConfig.general.theme);
    mtInterface.deactivateModule(&configEditor);
    mtInterface.activateModule(&configEditor, 0);
}

void setDisplayFont()
{
    if (mtConfig.general.font >= ThemeManager::getFontCount())
        mtConfig.general.font = 0;
    ThemeManager::applyFont(mtConfig.general.font);
    mtInterface.deactivateModule(&configEditor);
    mtInterface.activateModule(&configEditor, 0);
}

void setLineInOutType()
{
    if (mtConfig.general.lineInOutType > 2)
        mtConfig.general.lineInOutType = 2;
    engine.setLineInOutType(mtConfig.general.lineInOutType);
}

void setPadsBrightness()
{
    if (mtConfig.general.padsBrightness > 2)
        mtConfig.general.padsBrightness = 2;

    mtConfig.values.padsLightBack =
      (PADS_LIGHT_BACK_DEFAULT * (mtConfig.general.padsBrightness + 1)) / 3;
    mtConfig.values.padsLightFront =
      (PADS_LIGHT_FRONT_DEFAULT * (mtConfig.general.padsBrightness + 1)) / 3;
    mtConfig.values.padsLightBackDim =
      ((PADS_LIGHT_BACK_DEFAULT / 2) * (mtConfig.general.padsBrightness + 1)) / 3;
}

void setPadBoardValues()
{
    mtPadBoard.setPadNotes(mtProject.values.padBoardScale,
                           mtProject.values.padBoardNoteOffset,
                           mtProject.values.padBoardRootNote);
    newFileManager.setProjectStructChanged();
}

void setAntialiasingEnable()
{
    engine.setInterpolationEnable(mtProject.values.antialiasingEnable);
    newFileManager.setProjectStructChanged();
}

void setLimiterMode()
{
    engine.setLimiterAttack(mtProject.values.limiterAttack);
    engine.setLimiterRelease(mtProject.values.limiterRelease);
    newFileManager.setProjectStructChanged();
}

void setSendsMode()
{
    engine.refreshAllSends();
    newFileManager.setProjectStructChanged();
}

void setHeadroomMode()
{
    engine.setHeadroomEnable((bool)mtProject.values.isHeadroomEnabled);
    newFileManager.setProjectStructChanged();
}

void setSnapToZeroMode()
{
    engine.setSnapToZeroEnable((bool)mtProject.values.isSnapToZeroEnabled);
    newFileManager.setProjectStructChanged();
}

void setMetronomValues()
{
    // wlaczenie wylaczenie metronomu
    // aktualizajca metrum
    // volume mozna brac pod uwage na bierzaco tam gdzie przetwarzanie metronomu

    // mtConfig.metronome.state;
    // mtConfig.metronome.timeSignature;
}

void setSlaveClockCompValue()
{
    sequencer.setExternalClockComp(mtConfig.midi.slaveClockComp);
}

void resetConfigShowPopup()
{
    configEditor.showResetConfigPopout();
    configEditor.ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);
}

void usbStorageEnter()
{
    if (mtInterface.getPlatform()->getUsb()->isHostMode())
    {
        mtPopups.showInfoPopup("USB is in Host mode.", "Set Port mode to Device first.");
        return;
    }

    startFileTransferMode();
}

void usbAudioChange()
{
    Usb* usb = mtInterface.getPlatform()->getUsb();

    switch (mtConfig.common.usbAudioMode)
    {
        case 1:
            usb->setClassState(Usb::ClassType::kAudioIn, true);
            usb->setClassState(Usb::ClassType::kAudioOut, false);
            break;
        case 2:
            usb->setClassState(Usb::ClassType::kAudioIn, false);
            usb->setClassState(Usb::ClassType::kAudioOut, true);
            break;
        case 3:
            usb->setClassState(Usb::ClassType::kAudioIn, true);
            usb->setClassState(Usb::ClassType::kAudioOut, true);
            break;
        default:
            usb->setClassState(Usb::ClassType::kAudioIn, false);
            usb->setClassState(Usb::ClassType::kAudioOut, false);
            break;
    }
#ifdef PLATFORM_IMX
    engine.refreshOutputSourceOutsideTheRecorder();
#endif
    usb->reset();
}

void usbHostChange()
{
    Usb* usb = mtInterface.getPlatform()->getUsb();

    if (!usb->hasHostMode())
    {
        mtConfig.common.usbHostMode = 0;
        return;
    }

    usb->setHostMode(mtConfig.common.usbHostMode == 1);
}

// Labels for Config > USB > Status and Device 1/2
const char* usbHostDeviceLabel[kUsbHostDeviceLabels] = { "-", "-" };
const char* usbHostStatusLabel = "-";
const char* usbHostPortLabel = "-";
static char usbHostPortText[48];

static char usbHostDeviceNames[kUsbHostDeviceLabels][32];
static char usbHostStatusText[48];
static char usbHostPopupName[32]; // the popup keeps a pointer to its text, so it must outlive us

// Shows when the port changes mode, so it's clear whether host mode really started
static void usbHostStateUpdate(Usb* usb)
{
    static uint8_t lastState = 0;
    static elapsedMillis sinceStatus = 1000;

    uint8_t state = usb->getHostState();
    if (state != lastState)
    {
        lastState = state;
        // Port mode popups are detail: only with Config > MIDI > MIDI monitor on.
        // Device connect/disconnect popups always show (usbHostDevicesUpdate).
        if (mtConfig.midi.monitor != 0)
        switch (state)
        {
            case 1: mtPopups.show(0, "USB host mode on", "Waiting for MIDI devices"); break;
            case 2: mtPopups.show(0, "USB host mode failed", "Port stays in device mode"); break;
            default: mtPopups.show(0, "USB device mode", "Connect to a computer"); break;
        }
    }

    // Status line for Config > USB, refreshed a couple of times per second
    if (sinceStatus >= 500)
    {
        sinceStatus = 0;
        const char* status = usb->getHostStatus();
        strncpy(usbHostStatusText, status != nullptr ? status : "-", sizeof(usbHostStatusText) - 1);
        usbHostStatusLabel = usbHostStatusText;

        const char* port = usb->getHostPortDebug();
        strncpy(usbHostPortText, port != nullptr ? port : "-", sizeof(usbHostPortText) - 1);
        usbHostPortLabel = usbHostPortText;
    }
}

// Called continuously from the interface loop. When a USB MIDI device is connected or removed
// in host mode, briefly shows its name and refreshes the device list in Config > USB.
void usbHostDevicesUpdate()
{
    static uint16_t lastVersion = 0;
    static char shownNames[kUsbHostDeviceLabels][32] = {};

    Usb* usb = mtInterface.getPlatform()->getUsb();
    if (!usb->hasHostMode())
    {
        return;
    }

    usbHostStateUpdate(usb);

    uint16_t version = usb->getHostDeviceListVersion();
    if (version == lastVersion)
    {
        return;
    }
    lastVersion = version;

    const bool hostMode = usb->isHostMode();
    char current[kUsbHostDeviceLabels][32] = {};

    for (uint8_t i = 0; i < kUsbHostDeviceLabels; i++)
    {
        const char* name = hostMode ? usb->getHostDeviceName(i) : nullptr;
        if (name != nullptr)
        {
            strncpy(current[i], name, sizeof(current[i]) - 1);
        }
    }

    if (hostMode)
    {
        auto contains = [](char list[][32], const char* name)
        {
            for (uint8_t j = 0; j < kUsbHostDeviceLabels; j++)
            {
                if (strcmp(list[j], name) == 0)
                    return true;
            }
            return false;
        };

        for (uint8_t i = 0; i < kUsbHostDeviceLabels; i++)
        {
            if (current[i][0] != 0 && !contains(shownNames, current[i]))
            {
                strcpy(usbHostPopupName, current[i]);
                mtPopups.show(0, "USB MIDI connected", usbHostPopupName);
            }
            else if (shownNames[i][0] != 0 && !contains(current, shownNames[i]))
            {
                strcpy(usbHostPopupName, shownNames[i]);
                mtPopups.show(0, "USB MIDI disconnected", usbHostPopupName);
            }
        }
    }

    for (uint8_t i = 0; i < kUsbHostDeviceLabels; i++)
    {
        strcpy(shownNames[i], current[i]);
        strcpy(usbHostDeviceNames[i], current[i]);
        usbHostDeviceLabel[i] = current[i][0] != 0 ? usbHostDeviceNames[i]
                                : hostMode         ? "Not connected"
                                                   : "Host mode off";
    }
}

void checkFuncitonKey(int key)
{
    for (int idx = 0; idx < 8; idx++)
    {
        if (idx != key && mtConfig.interface.functionKeysBinding[idx] ==
                            mtConfig.interface.functionKeysBinding[key])
        {
            int firstFreeFunction = 0;
            bool assigned;

            for (uint8_t functionIdx = 0; functionIdx < kMenuModulesCount; functionIdx++)
            {
                assigned = false;
                for (uint8_t keyIdx = 0; keyIdx < 8; keyIdx++)
                {
                    if (mtConfig.interface.functionKeysBinding[keyIdx] == functionIdx)
                    {
                        assigned = true;
                    }
                }

                if (!assigned)
                {
                    firstFreeFunction = functionIdx;
                    break;
                }
            }

            mtConfig.interface.functionKeysBinding[idx] = firstFreeFunction;
        }
    }
}

void setFunctionKey1()
{
    checkFuncitonKey(0);
}

void setFunctionKey2()
{
    checkFuncitonKey(1);
}

void setFunctionKey3()
{
    checkFuncitonKey(2);
}

void setFunctionKey4()
{
    checkFuncitonKey(3);
}

void setFunctionKey5()
{
    checkFuncitonKey(4);
}

void setFunctionKey6()
{
    checkFuncitonKey(5);
}

void setFunctionKey7()
{
    checkFuncitonKey(6);
}

void setFunctionKey8()
{
    checkFuncitonKey(7);
}
