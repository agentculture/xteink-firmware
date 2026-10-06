#include "ResetBoot.h"

#include <BatteryMonitor.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <esp_sleep.h>
#include <esp_system.h>

namespace xteink::resetboot {

bool shouldBootAfterReset(const bool classifiedAsPowerButton, const bool holdVerified) {
  Inputs in;
  in.x3 = gpio.deviceIsX3();
  in.classifiedAsPowerButton = classifiedAsPowerButton;
  in.holdVerified = holdVerified;
  // Cheap early exit: everything but the X3 "not held" power-on is upstream's.
  if (!in.x3 || !classifiedAsPowerButton || holdVerified) return false;
  in.powerOnReset = esp_reset_reason() == ESP_RST_POWERON;
  in.sleepWakeCause = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  if (in.powerOnReset && !in.sleepWakeCause) {
    const BatteryMonitor battery;
    const auto status = battery.readStatus();
    in.percentKnown = status.percentageKnown;
    in.percent = status.percentage;
    in.millivoltsKnown = status.millivoltsKnown;
    in.millivolts = status.millivolts;
  }
  const bool boot = bootAfterReset(in);
  LOG_INF("MAIN", "Power-on without held button: battery %s%u%% %umV -> %s", in.percentKnown ? "" : "unknown ",
          static_cast<unsigned>(in.percent), static_cast<unsigned>(in.millivolts),
          boot ? "reset key, booting" : "sleeping");
  return boot;
}

}  // namespace xteink::resetboot
