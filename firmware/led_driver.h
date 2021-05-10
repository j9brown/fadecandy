/*
 * Simplified LED driver for SK6812 pixels based on OctoWS2811.
 */

/*  OctoWS2811 - High Performance WS2811 LED Display Library
    http://www.pjrc.com/teensy/td_libs_OctoWS2811.html
    Copyright (c) 2013 Paul Stoffregen, PJRC.COM, LLC

    Zero-copy variant (OctoWS2811z) hacked up by Micah Elizabeth Scott.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <utility>

#include "glimmer/led_timings.h"

namespace glimmer::led {

// Computes the size of buffer required to write up to 8 strips of given length in parallel.
constexpr size_t bufferSize(size_t ledsPerStrip) { return ledsPerStrip * 24; }

// Initialize the GPIOs and DMA for LED output.
// May be called multiple times to reinitialize the array if the configuration changes.
// Blocks until all prior writes have completed.
// Returns true if successful, false if the parameters are invalid.
bool init(size_t ledsPerStrip, const Timings& timings);

// Writes a buffer of encoded LED data to the DMA engine.
// This operation completes asynchronously. Subsequent writes will block
// until all prior writes have completed and the LEDs are ready to receive
// new data.
void write(const uint8_t* buffer);

// Pushes pixels into a DMA buffer.
// For SK6812, WS2812, WS2811, and similar strips, the input is an array of
// 24-bit GRB values and the output is 24 bytes or swizzled pixeliness.
template <size_t ledStrips>
void pushPixels(uint32_t*& out, const uint32_t pixels[ledStrips]) {
    uint32_t o0 = 0, o1 = 0, o2 = 0, o3 = 0, o4 = 0, o5 = 0;

    // Use the BFI (bit field insert) instruction to efficiently remap bits for DMA.
    // It seems that we can't rely on newer versions of GCC (e.g. 10.2.1) to optimize bit-field
    // expressions using BFI and prefers instead to emit sequences of ANDs and ORs so we need
    // to use assembly to get the desired instructions.
#if 1
    #define LED_LSR_BFI(R, P, RS, PS) \
        asm ("bfi %0, %1, %2, #1" : "+r" (R) : "r" (P >> (PS)), "M" (RS))
#else
    #define LED_LSR_BFI(R, P, RS, PS) \
        if constexpr (PS == 0) \
            asm ("bfi %0, %1, %2, #1" : "+r" (R) : "r" (P), "M" (RS)); \
        else \
            asm ("lsr ip, %1, %3; bfi %0, ip, %2, #1" : "+r" (R) : "r" (P), "M" (RS), "M" (PS))
#endif
    #define LED_LSR_BFI4(R, P, RS, PS) \
        LED_LSR_BFI(R, P, RS, PS + 3); \
        LED_LSR_BFI(R, P, RS + 8, PS + 2); \
        LED_LSR_BFI(R, P, RS + 16, PS + 1); \
        LED_LSR_BFI(R, P, RS + 24, PS);
    #define LED_SWIZZLE(C) if constexpr (C < ledStrips) { \
        uint32_t p = pixels[C]; \
        LED_LSR_BFI4(o5, p, C, 0); \
        LED_LSR_BFI4(o4, p, C, 4); \
        LED_LSR_BFI4(o3, p, C, 8); \
        LED_LSR_BFI4(o2, p, C, 12); \
        LED_LSR_BFI4(o1, p, C, 16); \
        LED_LSR_BFI4(o0, p, C, 20); \
    }
    LED_SWIZZLE(0)
    LED_SWIZZLE(1)
    LED_SWIZZLE(2)
    LED_SWIZZLE(3)
    LED_SWIZZLE(4)
    LED_SWIZZLE(5)
    LED_SWIZZLE(6)
    LED_SWIZZLE(7)
    #undef LED_SWIZZLE
    #undef LED_LSR_BFI4
    #undef LED_BFI

    *(out++) = o0;
    *(out++) = o1;
    *(out++) = o2;
    *(out++) = o3;
    *(out++) = o4;
    *(out++) = o5;
}

// Fills a DMA buffer where the number of strips is determined at compile time.
// |Sampler| is a lambda function to generate the pixels to output
//      signature: uint32_t Sampler(size_t strip, size_t pixel)
template <size_t ledStrips, typename Sampler>
void updateBuffer(uint8_t* buffer, size_t ledsPerStrip, Sampler sampler) {
    uint32_t* out = reinterpret_cast<uint32_t*>(buffer);
    for (size_t i = 0 ; i < ledsPerStrip; ++i) {
        uint32_t pixels[ledStrips];
        if constexpr (ledStrips > 0) pixels[0] = sampler(0, i);
        if constexpr (ledStrips > 1) pixels[1] = sampler(1, i);
        if constexpr (ledStrips > 2) pixels[2] = sampler(2, i);
        if constexpr (ledStrips > 3) pixels[3] = sampler(3, i);
        if constexpr (ledStrips > 4) pixels[4] = sampler(4, i);
        if constexpr (ledStrips > 5) pixels[5] = sampler(5, i);
        if constexpr (ledStrips > 6) pixels[6] = sampler(6, i);
        if constexpr (ledStrips > 7) pixels[7] = sampler(7, i);
        pushPixels<ledStrips>(out, pixels);
    }
}

// Fills a DMA buffer where the number of LED strips is determined at runtime.
// |Sampler| is a lambda function to retrieve a pixel from each strip
//      signature: uint32_t Sampler(size_t strip, size_t pixel)
template <typename Sampler>
void updateBuffer(uint8_t* buffer, size_t ledStrips, size_t ledsPerStrip, Sampler sampler) {
    switch (ledStrips) {
        case 1: updateBuffer<1, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 2: updateBuffer<2, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 3: updateBuffer<3, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 4: updateBuffer<4, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 5: updateBuffer<5, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 6: updateBuffer<6, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 7: updateBuffer<7, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        case 8: updateBuffer<8, Sampler>(buffer, ledsPerStrip, std::move(sampler)); break;
        default: __builtin_unreachable(); break;
    }
}

} // namespace glimmer::led
