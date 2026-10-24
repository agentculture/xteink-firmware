#pragma once

#include <memory>
#include <string>

#include "activities/Activity.h"

// "Provision via USB": the only screen in which the firmware accepts xteink
// provisioning messages (docs/provisioning.md). While it is open it owns the USB
// serial RX path; everywhere else main.cpp answers provisioning lines with
// not_in_provisioning_mode. Never shows or logs the key, passwords, or URLs.
class ProvisionUsbActivity final : public Activity {
 public:
  explicit ProvisionUsbActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ProvisionUsb", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // keep USB polling prompt, no power saving
  bool preventAutoSleep() override { return true; }
  void render(RenderLock&&) override;

  // True while the screen is open; main.cpp skips its own serial reader then.
  static bool isActive();

 private:
  void handleLine(size_t len);
  void sendLine(const std::string& line);

  std::unique_ptr<char[]> lineBuf;  // kMaxLineLen + 1, allocated once per screen visit
  size_t lineLen = 0;
  bool overflowed = false;

  bool provisioned = false;
  size_t networksSaved = 0;
  std::string keyId;  // 8 hex chars, shown on screen (not secret)
  const char* lastError = nullptr;
};
