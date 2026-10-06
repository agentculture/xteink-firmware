#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Reader zoom-mode state (xteink fork). Pure logic, no hardware or renderer
// dependencies, so it is host-testable: the activity owns one, feeds it button
// events, and applies the result.
//
// The point being previewed is only a selection here. Stepping never touches
// SETTINGS or the book layout; the reader reflows once, when the mode is left
// through commit(). cancel() restores the size zoom mode was entered with.
class ZoomMode {
 public:
  struct Exit {
    bool changed;  // the book must be reflowed at `pt`
    uint8_t pt;    // point size to leave the reader with
  };

  bool active() const { return active_; }

  // `sizes` ascending and non-empty (readerFontPointSizes()'s contract). The
  // starting selection is the entry nearest `currentPt`, ties to the smaller.
  // Returns false (and stays inactive) when `sizes` is empty.
  bool enter(std::vector<uint8_t> sizes, const uint8_t currentPt) {
    if (sizes.empty()) return false;
    sizes_ = std::move(sizes);
    index_ = nearestIndex(currentPt);
    originalIndex_ = index_;
    active_ = true;
    return true;
  }

  // One step toward a smaller / larger point size. False at the end of the
  // list (no change, nothing to redraw).
  bool stepSmaller() {
    if (!active_ || index_ == 0) return false;
    --index_;
    return true;
  }
  bool stepLarger() {
    if (!active_ || index_ + 1 >= sizes_.size()) return false;
    ++index_;
    return true;
  }

  // Leave zoom mode keeping the selection.
  Exit commit() {
    const Exit out{index_ != originalIndex_, selectedPt()};
    reset();
    return out;
  }
  // Leave zoom mode discarding the selection; the original size is untouched.
  Exit cancel() {
    const Exit out{false, sizes_.empty() ? uint8_t{0} : sizes_[originalIndex_]};
    reset();
    return out;
  }

  uint8_t selectedPt() const { return sizes_.empty() ? uint8_t{0} : sizes_[index_]; }
  const std::vector<uint8_t>& sizes() const { return sizes_; }
  size_t selectedIndex() const { return index_; }

 private:
  size_t nearestIndex(const uint8_t pt) const {
    size_t best = 0;
    unsigned bestDelta = delta(sizes_[0], pt);
    for (size_t i = 1; i < sizes_.size(); ++i) {
      const unsigned d = delta(sizes_[i], pt);
      if (d < bestDelta) {  // strictly less: a tie keeps the smaller size
        best = i;
        bestDelta = d;
      }
    }
    return best;
  }
  static unsigned delta(const uint8_t a, const uint8_t b) { return a > b ? a - b : b - a; }
  void reset() {
    active_ = false;
    sizes_.clear();
    sizes_.shrink_to_fit();  // the scale only needs its list while zoom mode is up
    index_ = originalIndex_ = 0;
  }

  bool active_ = false;
  std::vector<uint8_t> sizes_;
  size_t index_ = 0;
  size_t originalIndex_ = 0;
};
