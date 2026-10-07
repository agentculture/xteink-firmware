#include "KeyDiag.h"

#if XTEINK_KEY_DIAG

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <InputManager.h>
#include <Logging.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace xteink::keydiag {

namespace {
constexpr uint32_t kSampleIntervalMs = 20;
constexpr uint32_t kMinLogIntervalMs = 100;
// The ladder idles at the ADC full-scale rail (~4095) and every SDK button
// band sits at or below InputManager::ADC_NO_BUTTON (3900, InputManager.h);
// HalGPIO::rawInputActive() uses the same 4000 split. A reading in (3900,
// 4000) is off the rail but decodes to nothing.
constexpr int kIdleRailMin = 4000;
// While a key is held (or undecoded), report a reading that moves this far
// from the last logged one, so a second key on the same ladder shows up.
constexpr int kRawDelta = 150;
constexpr uint8_t kLogicalButtons = 7;  // InputManager BTN_BACK..BTN_POWER

uint32_t lastSampleMs = 0;
uint32_t lastLogMs = 0;
bool logged = false;
uint8_t lastMask = 0;
int lastPower = -1;
bool lastUndecoded = false;
int lastAdc1 = -1;
int lastAdc2 = -1;
}  // namespace

void poll() {
  // Only the X3/X4 ladder: on other boards GPIO1/2 serve other functions (the
  // X4 Pro's GPIO2 is its touch power enable) and must not be re-read as ADC.
  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder) return;
  const uint32_t now = millis();
  if (logged && now - lastSampleMs < kSampleIntervalMs) return;
  lastSampleMs = now;

  uint8_t mask = 0;
  for (uint8_t i = 0; i < kLogicalButtons; ++i) {
    if (gpio.isPressed(i)) mask |= static_cast<uint8_t>(1u << i);
  }
  const int adc1 = analogRead(InputManager::BUTTON_ADC_PIN_1);
  const int adc2 = analogRead(InputManager::BUTTON_ADC_PIN_2);
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  const int power = powerPin >= 0 ? digitalRead(powerPin) : -1;
  const bool undecoded = mask == 0 && (adc1 < kIdleRailMin || adc2 < kIdleRailMin);

  bool changed = !logged || mask != lastMask || power != lastPower || undecoded != lastUndecoded;
  if (!changed && (mask != 0 || undecoded)) {
    changed = abs(adc1 - lastAdc1) > kRawDelta || abs(adc2 - lastAdc2) > kRawDelta;
  }
  if (!changed) return;
  // Rate limit: leave the last-logged state alone so the change is reported
  // on the first sample after the interval.
  if (logged && now - lastLogMs < kMinLogIntervalMs) return;

  logged = true;
  lastLogMs = now;
  lastMask = mask;
  lastPower = power;
  lastUndecoded = undecoded;
  lastAdc1 = adc1;
  lastAdc2 = adc2;

  char names[64];
  size_t len = 0;
  names[0] = '\0';
  for (uint8_t i = 0; i < kLogicalButtons && len < sizeof(names); ++i) {
    if ((mask & (1u << i)) == 0) continue;
    const int n = snprintf(names + len, sizeof(names) - len, "%s%s", len ? "+" : "", InputManager::getButtonName(i));
    if (n < 0) break;
    len += static_cast<size_t>(n);
  }
  if (mask == 0) snprintf(names, sizeof(names), "%s", undecoded ? "UNDECODED" : "none");
  LOG_INF("XKEY", "adc1=%d adc2=%d pwr=%d mask=0x%02x -> %s", adc1, adc2, power, static_cast<unsigned>(mask), names);
}

}  // namespace xteink::keydiag

#endif  // XTEINK_KEY_DIAG
