/*
 * Fadecandy Firmware
 * 
 * Copyright (c) 2013 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

/*** Memory limits ***/

// Configures how many frame buffers are allocated.
//
// This setting determines whether interpolation is supported.
//
// Possible values:
//     2: frames are double-buffered, uses less memory
//     3: frames are triple-buffered, supports interpolation
#define CONFIG_MAX_FRAME_BUFFERS (3)

// Configures the maximum number of USB packets transmitted per frame
// depending on whether frames are double-buffered or triple-buffers.
//
// This setting determines how much memory is used by each frame buffer.
// The maximum number of pixels per frame depends on this value and on the
// color format chosen at runtime (typically 15 or 21 pixels per frame).
//
// Each packet is 64 bytes.
#define CONFIG_MAX_PACKETS_PER_DOUBLE_BUFFERED_FRAME (72) // 4608 bytes
#define CONFIG_MAX_PACKETS_PER_TRIPLE_BUFFERED_FRAME (48) // 3072 bytes

// Derive the maximum number of packets to allocate for frame buffers of any kind.
#define CONFIG_MAX_PACKETS_FOR_FRAMEBUFFERS_(x, y) ((x) > (y) ? (x) : (y))
#define CONFIG_MAX_PACKETS_FOR_FRAMEBUFFERS CONFIG_MAX_PACKETS_FOR_FRAMEBUFFERS_( \
    (CONFIG_MAX_FRAME_BUFFERS >= 2 ? 2 * CONFIG_MAX_PACKETS_PER_DOUBLE_BUFFERED_FRAME : 0), \
    (CONFIG_MAX_FRAME_BUFFERS >= 3 ? 3 * CONFIG_MAX_PACKETS_PER_TRIPLE_BUFFERED_FRAME : 0))

// Configures the maximum number of LED pixels per strip supported.
//
// This setting determines how much memory is used for DMA.  The same amount
// of memory is needed to support strips of a given length regardless of the
// number of parallel strips (up to 8).
//
// Each unit of length requires 48 bytes (because there are two output buffers).
#define CONFIG_MAX_LEDS_PER_STRIP (120) // 5760 bytes

/*** Renderer algorithm configuration ***/

// List of tags for the renderers to compile into the firmware.
#define CONFIG_RENDERER(fmt, ditherMode, interpolateMode) \
        ::glimmer::render::RendererTag<::glimmer::render::ColorFormat::fmt, \
                ::glimmer::render::DitherMode::ditherMode, \
                ::glimmer::render::InterpolateMode::interpolateMode>
#define CONFIG_RENDERERS \
        CONFIG_RENDERER(R8G8B8, NONE, NONE), \
        CONFIG_RENDERER(R8G8B8, TEMPORAL, NONE), \
        CONFIG_RENDERER(R8G8B8, NONE, LINEAR), \
        CONFIG_RENDERER(R8G8B8, TEMPORAL, LINEAR), \
        CONFIG_RENDERER(R11G11B11, NONE, NONE), \
        CONFIG_RENDERER(R11G11B11, TEMPORAL, NONE), \
        CONFIG_RENDERER(R11G11B11, NONE, LINEAR), \
        CONFIG_RENDERER(R11G11B11, TEMPORAL, LINEAR) \

/*** USB stack configuration ***/

// Number of USB buffers to allocate for data transfer.
// The USB stack needs enough buffers to hold the contents of all frame buffers
// (zero-copy) plus 4 more to keep the buffer descriptor table full to allow
// for packets in flight in both directions.
#ifndef CONFIG_NUM_USB_BUFFERS
#define CONFIG_NUM_USB_BUFFERS (CONFIG_MAX_PACKETS_FOR_FRAMEBUFFERS + 4)
#endif

// Quick sanity check for memory capacity (might still fail at link time if we're
// close to the limit).
#if (CONFIG_MAX_LEDS_PER_STRIP * 24 * 2) + (CONFIG_NUM_USB_BUFFERS * 64) >= 16384
#error "Buffers won't fit.  Try adjusting limits in config.h."
#endif

// USB descriptor information
#define CONFIG_VENDOR_ID               0x1d50    // OpenMoko
#define CONFIG_PRODUCT_ID              0x607a    // Assigned to Fadecandy project
#define CONFIG_DEVICE_VER              0x0390	  // BCD device version
#define CONFIG_DEVICE_VER_STRING       "3.90"
#define CONFIG_MANUFACTURER_NAME         {'s','c','a','n','l','i','m','e'}
#define CONFIG_MANUFACTURER_NAME_LEN     8
#define CONFIG_PRODUCT_NAME              {'F','a','d','e','c','a','n','d','y'}
#define CONFIG_PRODUCT_NAME_LEN          9
#define CONFIG_DFU_NAME                  {'F','a','d','e','c','a','n','d','y',' ','B','o','o','t','l','o','a','d','e','r'}
#define CONFIG_DFU_NAME_LEN              20

#ifdef __cplusplus
namespace glimmer::config {

constexpr size_t maxFrameBuffers = CONFIG_MAX_FRAME_BUFFERS;
constexpr size_t maxPacketsPerDoubleBufferedFrame = CONFIG_MAX_PACKETS_PER_DOUBLE_BUFFERED_FRAME;
constexpr size_t maxPacketsPerTripleBufferedFrame = CONFIG_MAX_PACKETS_PER_TRIPLE_BUFFERED_FRAME;
constexpr size_t maxLedsPerStrip = CONFIG_MAX_LEDS_PER_STRIP;

} // glimmer::config
#endif
