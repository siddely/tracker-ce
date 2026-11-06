#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_

void restartConfigAllActions();
void initConfigValuesAfterReadConfig();

void setRadioRegion();
void setDisplayBrightness();
void setDisplayTheme();
void setDisplayFont();
void refreshThemeAndFontMenuOptions();
void setPadsBrightness();
void setLineInOutType();

void setPadBoardValues();
void setAntialiasingEnable();
void setLimiterMode();
void setSendsMode();
void setHeadroomMode();
void setSnapToZeroMode();

void setMetronomValues();
void setSlaveClockCompValue();

void openCreditsAction();

void startFileTransferMode();

void openManual1Action();
void openManual2Action();
void openManual3Action();

void setFunctionKey1();
void setFunctionKey2();
void setFunctionKey3();
void setFunctionKey4();
void setFunctionKey5();
void setFunctionKey6();
void setFunctionKey7();
void setFunctionKey8();

void resetConfigShowPopup();
void usbStorageEnter();
void usbAudioChange();
void usbHostChange();

constexpr uint8_t kUsbHostDeviceLabels = 2;
extern const char* usbHostDeviceLabel[kUsbHostDeviceLabels];
extern const char* usbHostStatusLabel;
extern const char* usbHostPortLabel;
void usbHostDevicesUpdate();
void openUsbLogAction();
void usbLogViewUpdate(); // keeps the USB log screen live while it is open

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_ */
