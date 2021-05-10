/*
 * USB protocol definitions for the board.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "led_timings.h"

namespace glimmer {
namespace protocol {

// Packet type information encoded in the first byte of each packet.
enum class PacketType : uint8_t {
    // The high bit indicates whether the packet is a control message.
    // If not set, the packet is part of a frame and the type field
    // encodes the frame packet index.
    controlFlag = 0x80,
    // A configuration packet.
    config = controlFlag | 0x00,
    // A debugging packet.
    debug = controlFlag | 0x01,
};
constexpr bool isControlPacket(PacketType type) {
    return static_cast<uint8_t>(type) & static_cast<uint8_t>(PacketType::controlFlag);
}

// Maximum index for a frame packet.
constexpr uint8_t framePacketMaxIndex = 0x7f;

// Contents of an image frame packet.
struct FramePacket {
    uint8_t index;     // index between 0 and |framePacketMaxIndex|
    uint8_t data[63];  // image data, representation depends on color format
};

// Color representations.
enum class ColorFormat : uint8_t {
    R8G8B8 = 0,     // 24-bit color
    R11G11B11 = 1,  // 33-bit color, blue LSBs are packed into a separate word
};

// Computes the number of pixels that can be stored in a USB packet, taking
// into account protocol overhead (1 header byte).
constexpr size_t pixelsPerPacket(ColorFormat fmt) {
    return fmt == ColorFormat::R8G8B8 ? 21 : 15;
}

// Computes the number of packets per frame.
constexpr size_t packetsPerFrame(size_t ledStrips, size_t ledsPerStrip, ColorFormat fmt) {
    return (ledStrips * ledsPerStrip + pixelsPerPacket(fmt) - 1) / pixelsPerPacket(fmt);
}

// Maximum number of packets per frame.
constexpr size_t maxPacketsPerFrame = framePacketMaxIndex + 1;

// The type of dither to apply to each pixel.
enum class DitherMode : uint8_t {
    NONE = 0,
    TEMPORAL = 1,
};

// The type of interpolation to apply between frames.
enum class InterpolateMode : uint8_t {
    NONE = 0,
    LINEAR = 1,
};

// The behavior of the indicator LED on the FadeCandy board itself.
enum class IndicatorMode : uint8_t {
    ACTIVITY = 0,  // blink when USB packets are received
    OFF = 1,
    ON = 2,
};

// Contents of a configuration packet.
struct ConfigPacket {
    PacketType type;      // set to |PacketType::config|
    uint8_t ledStrips;
    uint8_t ledsPerStrip;
    uint8_t maxDitherBits;
    ColorFormat colorFormat;
    DitherMode ditherMode;
    InterpolateMode interpolateMode;
    IndicatorMode indicatorMode;
    led::Timings timings;
};
constexpr ConfigPacket configPacketDefault = {
    PacketType::config, 8, 64, 3, ColorFormat::R11G11B11, DitherMode::TEMPORAL, InterpolateMode::LINEAR,
    IndicatorMode::ACTIVITY, led::timingsDefault
};

// Contents of a debugging packet.
struct DebugPacket {
    PacketType type;        // set to |PacketType::debug|
    uint8_t printStats;     // when 1, write statistics to the serial port periodically
};
constexpr DebugPacket debugPacketDefault = {
    PacketType::debug, 0
};

} // namespace protocol
} // namespace glimmer
