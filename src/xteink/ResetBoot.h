#pragma once

#include <cstdint>

// xteink fork (d5): boot after the X3's top key, which is wired to the chip's
// RESET (EN) line. An EN reset reports ESP_RST_POWERON with no sleep wakeup
// cause, the same as a button-energised cold boot, so upstream's
// HalGPIO::getWakeupReason() classifies it as WakeupReason::PowerButton and
// main.cpp's hold verification sends it straight back to sleep (the button is
// not held). This module turns that one case into an ordinary cold boot.
namespace xteink::resetboot {

// Below this the battery may not carry a boot (display refresh, SD mount),
// and a power-on that browns out would come straight back as another
// power-on: such boots keep upstream's sleep.
inline constexpr uint16_t MIN_BATTERY_PERCENT = 3;
inline constexpr uint16_t MIN_BATTERY_MV = 3400;

struct Inputs {
  bool x3 = false;                       // runtime X3 detection (the only board with a RESET key)
  bool powerOnReset = false;             // esp_reset_reason() == ESP_RST_POWERON
  bool sleepWakeCause = false;           // esp_sleep_get_wakeup_cause() != UNDEFINED
  bool classifiedAsPowerButton = false;  // HalGPIO::getWakeupReason() == PowerButton
  bool holdVerified = false;             // HalGPIO::verifyPowerButtonWakeup() passed
  bool percentKnown = false;             // fuel gauge read succeeded
  uint16_t percent = 0;
  bool millivoltsKnown = false;
  uint16_t millivolts = 0;
};

// True when the boot should proceed as a normal cold boot (WakeupReason::Other)
// instead of upstream's "not held through verification, sleeping".
constexpr bool bootAfterReset(const Inputs& in) {
  if (!in.x3 || !in.classifiedAsPowerButton || in.holdVerified) return false;  // upstream path
  if (!in.powerOnReset || in.sleepWakeCause) return false;  // a real deep-sleep wake: keep verification
  // Boot only when the gauge positively reports a battery that can carry it;
  // an unreadable gauge keeps upstream's sleep, so no new boot loop is possible.
  if (!in.percentKnown || in.percent < MIN_BATTERY_PERCENT) return false;
  if (in.millivoltsKnown && in.millivolts < MIN_BATTERY_MV) return false;
  return true;
}

#ifdef ARDUINO
// Reads the reset reason, wake cause and battery gauge and applies
// bootAfterReset(). Returns true when main.cpp should boot normally.
bool shouldBootAfterReset(bool classifiedAsPowerButton, bool holdVerified);
#endif

}  // namespace xteink::resetboot
