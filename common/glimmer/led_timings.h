/*
 * LED timing parameters for WS2811, WS2812B, and SK6812 strips.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace glimmer {
namespace led {

// Timings for the LED output protocol.
// Can be tuned to optimize performance for various models.
struct Timings {
    // LED strip frequency in Hz.
    uint32_t frequency;
    // Reset interval in microseconds.
    uint32_t resetInterval;
    // On-time percentage for 0s and 1s, as a fraction of 255.
    uint32_t t0h, t1h;
};

// Perform basic sanity checks for LED timings to avoid crashing the board
// during experiments even if those values might not actually work.
// - frequency: 100 kHz to 2 MHz
//              typically 400 or 800 kHz but some parts can be safely overclocked
//              to 1 MHz or more
// - resetInterval: less than 5 ms, because the watchdog trips at 10 ms
//                  typically in the range of 50 to 300 us
// - t0h, t1h: between 1 and 255, t1h greater than t0h
//             typically set to obtain roughly 300 us and 600 us intervals
//             with a safety margin for accurate signaling depending on what
//             the part tolerates (especially when overclocking)
inline bool validateTimings(const Timings& timings) {
    return timings.frequency >= 100000 && timings.frequency <= 2000000 &&
            timings.resetInterval <= 5000 &&
            timings.t0h > 0 && timings.t1h > timings.t0h && timings.t1h <= 255;
}

// OctoWS2811 defaults.
//
// Waveform timing: these set the high time for a 0 and 1 bit, as a fraction of
// the total 800 kHz or 400 kHz clock cycle.  The scale is 0 to 255.  The Worldsemi
// datasheet seems T1H should be 600 ns of a 1250 ns cycle, or 48%.  That may
// erroneous information?  Other sources reason the chip actually samples the
// line close to the center of each bit time, so T1H should be 80% if TOH is 20%.
// The chips appear to work based on a simple one-shot delay triggered by the
// rising edge.  At least 1 chip tested retransmits 0 as a 330 ns pulse (26%) and
// a 1 as a 660 ns pulse (53%).  Perhaps it's actually sampling near 500 ns?
// There doesn't seem to be any advantage to making T1H less, as long as there
// is sufficient low time before the end of the cycle, so the next rising edge
// can be detected.  T0H has been lengthened slightly, because the pulse can
// narrow if the DMA controller has extra latency during bus arbitration.  If you
// have an insight about tuning these parameters AND you have actually tested on
// real LED strips, please contact paul@pjrc.com.  Please do not email based only
// on reading the datasheets and purely theoretical analysis.
constexpr Timings timingsDefault { 800000, 300, 60, 176 };

// SK6812 allows a shorter reset interval and can be overclocked reliably up to 1 MHz.
constexpr Timings timingsSK6812 { 800000, 100, 56, 172 };
constexpr Timings timingsSK6812Fast { 900000, 100, 44, 150 };
constexpr Timings timingsSK6812Extreme { 1000000, 80, 40, 140 };

struct NamedTimings {
    const char* name;
    const Timings timings;
};
constexpr NamedTimings namedTimings[] = {
    { "default", timingsDefault },
    { "sk6812", timingsSK6812 },
    { "sk6812-fast", timingsSK6812Fast },
    { "sk6812-extreme", timingsSK6812Extreme },
};

inline const Timings* timingsByName(const char* name) {
    for (const auto& elem : namedTimings) {
        if (strcmp(elem.name, name) == 0) return &elem.timings;
    }
    return nullptr;
}

} // namespace led
} // namespace glimmer
