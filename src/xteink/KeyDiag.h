#pragma once

// xteink fork: debug-only key diagnostic for the X3 (8 physical keys, 7
// logical ones in freeink-sdk's XteinkAdcLadder decode). Logs one [XKEY] line
// per input state change: both raw ladder ADC readings, the power GPIO level
// and the logical buttons they decode to, plus readings that leave the idle
// rail without decoding to any button. How to read it: docs/test-x3.md "Key
// mapping diagnostic". Compiled only into serial-logging debug builds
// (ENABLE_SERIAL_LOG with LOG_LEVEL >= 2, i.e. env:default); elsewhere poll()
// is an empty inline function and the module adds no code.

#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
#define XTEINK_KEY_DIAG 1
#else
#define XTEINK_KEY_DIAG 0
#endif

namespace xteink::keydiag {

#if XTEINK_KEY_DIAG
// Call once per main-loop pass, after the input update. Self rate-limited:
// samples at most every 20 ms and logs at most every 100 ms; does nothing on
// boards without the Xteink ADC ladder.
void poll();
#else
inline void poll() {}
#endif

}  // namespace xteink::keydiag
