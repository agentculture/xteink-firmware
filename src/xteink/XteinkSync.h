#pragma once
// xteink sync client (device protocol v1, docs/xteink/sync.md): status ->
// queue -> download to microSD -> sha256 verify -> ack -> mirror deletes ->
// status. Runs on the calling (main loop) task over an already-joined Wi-Fi
// station; it is never started while a book is open, so reading never waits
// on the network.
#include <cstddef>
#include <cstdint>

#include "SyncProtocol.h"

class GfxRenderer;

namespace xteink::sync {

struct Progress {
  uint16_t itemIndex = 0;  // 1-based item being downloaded; 0 = talking to the server
  uint16_t itemCount = 0;
  size_t bytes = 0;
  size_t total = 0;
};

// Plain function pointers (no std::function): called on the syncing task.
struct Callbacks {
  void* ctx = nullptr;
  void (*onProgress)(void* ctx, const Progress& progress) = nullptr;
  // Polled while transferring; true stops the sync (ECANCEL, not persisted).
  bool (*shouldCancel)(void* ctx) = nullptr;
};

struct Outcome {
  Error error = Error::None;  // first fatal or per-item error
  uint16_t delivered = 0;
  uint16_t deleted = 0;
  uint16_t skipped = 0;  // queued but sd_full
  uint16_t failed = 0;
};

// One full sync. `renderer` (optional) lets it drop rebuildable SD font caches
// before TLS. `itemBudgetMs` > 0 stops starting new downloads after that long
// (the item in flight completes); later items stay queued for the next sync.
// The result is persisted for the home status line unless cancelled.
Outcome run(const GfxRenderer* renderer, const Callbacks& callbacks, uint32_t itemBudgetMs = 0);

// Wi-Fi join hook (WifiSelectionActivity): runs a bounded sync when a device
// key is provisioned, no book is open and the heap allows it.
void onStationJoined(GfxRenderer& renderer);

// Set while XteinkSyncActivity owns the join, so the hook does not sync twice.
void setJoinHookSuppressed(bool suppressed);

// The home status line ("Synced 14:02 · 2 new", "Sync error E401"). False when
// there is nothing to show. Safe to call from the render task.
bool statusLine(char* buf, size_t len);

// Draws statusLine() centered just above the button hints, if that band is
// below `contentBottom` (the last drawn home row); otherwise draws nothing.
void drawHomeStatusLine(const GfxRenderer& renderer, int contentBottom);

}  // namespace xteink::sync
