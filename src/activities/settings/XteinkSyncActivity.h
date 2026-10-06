#pragma once

#include "activities/Activity.h"
#include "xteink/XteinkSync.h"

// "Sync books now" (Settings > System). Reboots once onto a fresh heap (the
// verified TLS handshake to the tunnel needs ~40 KB contiguous-ish heap), joins
// Wi-Fi through WifiSelectionActivity (saved networks auto-connect), runs one
// xteink sync with a progress screen and shows the result. Back cancels.
// Leaving reboots to Home like the other Wi-Fi screens.
class XteinkSyncActivity final : public Activity {
  enum class State { Joining, Syncing, Done };

  // Set when the activity was started after the fresh-heap reboot.
  const bool freshHeap;
  State state = State::Joining;
  bool startPending = false;
  bool cancelRequested = false;
  unsigned long lastCancelPollMs = 0;
  unsigned long lastProgressRenderMs = 0;
  xteink::sync::Progress progress;  // guarded by RenderLock
  xteink::sync::Outcome outcome;    // guarded by RenderLock

  void onWifiJoined(bool connected);
  void runSync();
  static void onProgress(void* ctx, const xteink::sync::Progress& p);
  static bool shouldCancel(void* ctx);

 public:
  explicit XteinkSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool freshHeap = false)
      : Activity("XteinkSync", renderer, mappedInput), freshHeap(freshHeap) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state != State::Done; }
  bool skipLoopDelay() override { return true; }
};
