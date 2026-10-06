#include <gtest/gtest.h>

#include "src/xteink/ResetBoot.h"

using xteink::resetboot::bootAfterReset;
using xteink::resetboot::Inputs;

namespace {
// X3 top-key reset: POWERON, no wake cause, classified PowerButton, not held,
// healthy battery.
Inputs resetKey() {
  Inputs in;
  in.x3 = true;
  in.powerOnReset = true;
  in.sleepWakeCause = false;
  in.classifiedAsPowerButton = true;
  in.holdVerified = false;
  in.percentKnown = true;
  in.percent = 60;
  in.millivoltsKnown = true;
  in.millivolts = 3900;
  return in;
}
}  // namespace

TEST(ResetBootTest, X3ResetKeyBootsNormally) { EXPECT_TRUE(bootAfterReset(resetKey())); }

TEST(ResetBootTest, X4KeepsUpstreamSleep) {
  auto in = resetKey();
  in.x3 = false;
  EXPECT_FALSE(bootAfterReset(in));
}

TEST(ResetBootTest, DeepSleepWakeKeepsHoldVerification) {
  auto in = resetKey();
  in.powerOnReset = false;  // ESP_RST_DEEPSLEEP
  in.sleepWakeCause = true;
  EXPECT_FALSE(bootAfterReset(in));
  in.powerOnReset = true;  // any wake cause means a real wake
  EXPECT_FALSE(bootAfterReset(in));
}

TEST(ResetBootTest, HeldButtonOrOtherClassificationIsUntouched) {
  auto in = resetKey();
  in.holdVerified = true;  // normal power-button boot path already boots
  EXPECT_FALSE(bootAfterReset(in));
  in = resetKey();
  in.classifiedAsPowerButton = false;  // AfterUSBPower / AfterFlash / Other
  EXPECT_FALSE(bootAfterReset(in));
}

TEST(ResetBootTest, LowOrUnknownBatteryKeepsUpstreamSleep) {
  auto in = resetKey();
  in.percentKnown = false;
  EXPECT_FALSE(bootAfterReset(in));
  in = resetKey();
  in.percent = 2;
  EXPECT_FALSE(bootAfterReset(in));
  in = resetKey();
  in.millivolts = 3399;
  EXPECT_FALSE(bootAfterReset(in));
}

TEST(ResetBootTest, ThresholdsAreInclusive) {
  auto in = resetKey();
  in.percent = 3;
  in.millivolts = 3400;
  EXPECT_TRUE(bootAfterReset(in));
  in.millivoltsKnown = false;  // SoC alone is enough when voltage is unreadable
  EXPECT_TRUE(bootAfterReset(in));
}
