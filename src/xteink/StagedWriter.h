#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

// xteink fork (d7): staged SD writes for sync downloads.
//
// The HTTP client hands the download sink whatever the socket holds (1-2 KB,
// rarely sector-sized). Written straight through, nearly every 512-byte
// boundary costs SdFat a single-sector CMD24 plus a programming wait and a
// CMD13 (CHECK_FLASH_PROGRAMMING), each bounded by SdFat's fixed 300/600 ms
// card timeouts, and one failed write failed the whole book (ESD on the X3,
// 1.0-1.4 MB into a 6.5 MB item).
//
// Writer collects the stream in a caller-owned buffer and writes it only in
// full, buffer-sized blocks at buffer-aligned file offsets (plus the tail in
// finish()), so the card sees one multi-sector write per block. A block that
// fails is retried from the same file offset after a growing pause before the
// download is given up. SdFat returns all or nothing from a write (0 on any
// card error, never a short count), so the retry repositions and rewrites the
// whole block rather than looping on a remainder.
//
// Pure (host tests: test/xteink_staged_writer).
namespace xteink::staged {

// Pause before retry n (1-based). The sum stays far below the 30 s HTTP stall
// timeout, so the server connection survives a retried block.
inline constexpr uint32_t kRetryDelaysMs[] = {50, 250, 1000};
inline constexpr int kMaxAttempts = 1 + static_cast<int>(sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]));

struct Io {
  // Writes [data, data + len) at byte `offset` of the file and returns true
  // when every byte landed. After a failure it is called again with the same
  // arguments, so it must position the file at `offset` itself.
  std::function<bool(size_t offset, const uint8_t* data, size_t len)> writeAt;
  // Waits `ms` before a retry (feed any watchdog here).
  std::function<void(uint32_t ms)> pause;
};

class Writer {
 public:
  // `buf` must hold `capacity` bytes and outlive the Writer. A capacity that
  // is a multiple of the 512-byte sector keeps every block sector-aligned.
  Writer(uint8_t* buf, const size_t capacity, Io io) : buf_(buf), cap_(capacity), io_(std::move(io)) {}

  // Takes the next bytes of the stream. False once a block could not be
  // written after every retry; the writer then refuses further data.
  bool append(const uint8_t* data, size_t len) {
    if (failed_ || buf_ == nullptr || cap_ == 0) return false;
    while (len > 0) {
      const size_t n = std::min(len, cap_ - fill_);
      std::memcpy(buf_ + fill_, data, n);
      fill_ += n;
      accepted_ += n;
      data += n;
      len -= n;
      if (fill_ == cap_ && !flush()) return false;
    }
    return true;
  }

  // Writes the buffered tail. Call once the stream is complete.
  bool finish() { return !failed_ && flush(); }

  // The stream restarts from byte 0 (the server ignored a Range request).
  // Buffered bytes are dropped; the retry count is kept.
  void restart() {
    fill_ = 0;
    durable_ = 0;
    accepted_ = 0;
    failed_ = false;
  }

  size_t accepted() const { return accepted_; }  // stream bytes taken
  size_t durable() const { return durable_; }    // bytes writeAt() confirmed
  uint32_t retries() const { return retries_; }  // block rewrites, all time
  bool failed() const { return failed_; }

 private:
  bool flush() {
    if (fill_ == 0) return true;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
      if (attempt > 0) {
        ++retries_;
        if (io_.pause) io_.pause(kRetryDelaysMs[attempt - 1]);
      }
      if (io_.writeAt && io_.writeAt(durable_, buf_, fill_)) {
        durable_ += fill_;
        fill_ = 0;
        return true;
      }
    }
    failed_ = true;
    return false;
  }

  uint8_t* buf_;
  size_t cap_;
  Io io_;
  size_t fill_ = 0;
  size_t durable_ = 0;
  size_t accepted_ = 0;
  uint32_t retries_ = 0;
  bool failed_ = false;
};

}  // namespace xteink::staged
