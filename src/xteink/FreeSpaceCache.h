#pragma once

#include <cstdint>

// xteink fork (fix F): cached SD free space for the sync status report.
// HalStorage::sdFreeBytes() counts free clusters, which took 7 s on a 16 GB
// card on every sync. The value is cached in RAM and NVS with the trusted
// wall-clock time of the real query; a sync reuses it while it is younger
// than a day and leaves comfortable headroom, and adjusts it by the bytes it
// writes instead of rescanning. Pure (host tests: test/xteink_free_space).
namespace xteink::freespace {

inline constexpr int64_t TIGHT_BYTES = 64LL * 1024 * 1024;
inline constexpr int64_t MAX_AGE_S = 24LL * 3600;

struct Cache {
  int64_t freeBytes = -1;  // < 0: no cached value
  int64_t measuredAt = 0;  // trusted epoch seconds of the real query
};

// True when `cache` may stand in for a real query: a value exists, the clock
// is known and not behind it, it is younger than MAX_AGE_S, and after setting
// aside `pendingBytes` (the queue still to download, 0 before it is known) at
// least TIGHT_BYTES stay free. Otherwise the caller queries the card.
constexpr bool usable(const Cache& cache, const int64_t nowEpoch, const int64_t pendingBytes) {
  if (cache.freeBytes < 0 || nowEpoch <= 0 || cache.measuredAt <= 0) return false;
  if (nowEpoch < cache.measuredAt || nowEpoch - cache.measuredAt >= MAX_AGE_S) return false;
  const int64_t pending = pendingBytes > 0 ? pendingBytes : 0;
  return cache.freeBytes - pending >= TIGHT_BYTES;
}

// Cache after a real query. A failed query (<= 0) leaves no cached value.
constexpr Cache measured(const int64_t freeBytes, const int64_t nowEpoch) {
  if (freeBytes <= 0 || nowEpoch <= 0) return Cache{};
  return Cache{freeBytes, nowEpoch};
}

// Cache after the sync wrote `writtenBytes`. Deleted files are not credited
// (their size is not tracked), so the estimate errs low; the measurement time
// is kept, so the next real query still happens within a day.
constexpr Cache afterWrites(const Cache& cache, const int64_t writtenBytes) {
  if (cache.freeBytes < 0) return cache;
  const int64_t written = writtenBytes > 0 ? writtenBytes : 0;
  const int64_t left = cache.freeBytes > written ? cache.freeBytes - written : 0;
  return Cache{left, cache.measuredAt};
}

}  // namespace xteink::freespace
