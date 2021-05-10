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

#include <stddef.h>
#include <stdint.h>
#include <math.h>

#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

#include "arm/arm_math.h"
#include "config.h"
#include "debug.h"
#include "glimmer/protocol.h"
#include "hw/mk20dx128.h"
#include "hw/HardwareSerial.h"
#include "hw/pins_arduino.h"
#include "hw/core_pins.h"
#include "hw/usb_dev.h"
#include "led_driver.h"
#include "render.h"
#include "time.h"

void operator delete(void*, size_t) {}

namespace glimmer {

// Double-buffered DMA memory for raw bit planes of output.
using OutputBuffer = uint8_t[led::bufferSize(CONFIG_MAX_LEDS_PER_STRIP)];
__attribute__ ((section(".dmabuffers"), used))
OutputBuffer outputBuffers[2];
FLEXRAM_DATA uint8_t* frontOutputBuffer;
FLEXRAM_DATA uint8_t* backOutputBuffer;

// The renderer for presenting incoming frames.
FLEXRAM_DATA std::aligned_union_t<0, render::RendererHolder> rendererHolderStorage;
FLEXRAM_DATA render::RendererHolder* rendererHolder;

// Parameters provided by the host.
// These are set in an IRQ context so they need to be synchronized.
FLEXRAM_DATA protocol::ConfigPacket irqConfigPacket;
FLEXRAM_DATA volatile bool irqConfigChangedSinceLastLoop;
FLEXRAM_DATA protocol::DebugPacket irqDebugPacket;
FLEXRAM_DATA volatile bool irqDebugChangedSinceLastLoop;

// Set to true if any USB packets were handled since the last loop iteration.
// Used to show activity on the bus.
FLEXRAM_DATA volatile bool irqHandledUsbPacketsSinceLastLoop;

// Set to true if there's a new frame pending swap.
FLEXRAM_DATA volatile bool irqReceivedNewFrameSinceLastLoop;

// Copies of parameters needed in the main loop.
FLEXRAM_DATA protocol::IndicatorMode paramIndicatorMode;
FLEXRAM_DATA bool paramPrintStats;

// Statistics for debugging.
struct Stats {
    uint64_t startTime;
    uint32_t receivedFrameCount;
    uint32_t renderedFrameCount;
};
FLEXRAM_DATA Stats stats;

// Called from an interrupt context so we need to take care with synchronization.
// Must either take ownership of the packet or free it.
// Unrecognized packets are ignored to support protocol expansion.
bool handleUsbRxIrq(usb_packet_t* packet, size_t len) {
    // Zero out the tail of the packet to simplify validation and to avoid
    // accidentally processing uninitialized data as the protocol evolves.
    // In practice this has a negligible effect on performance since all but
    // the last frame packet is 64 bytes.
    for (size_t i = len; i < sizeof(packet->buf); i++)
        packet->buf[i] = 0;

    const protocol::PacketType header = static_cast<protocol::PacketType>(packet->buf[0]);
    if (protocol::isControlPacket(header)) {
        // Handle control requests.
        switch (header) {
            case protocol::PacketType::config:
                if (irqConfigChangedSinceLastLoop) return false; // defer packet
                memcpy(&irqConfigPacket, packet->buf, sizeof(irqConfigPacket));
                irqConfigChangedSinceLastLoop = true;
                break;
            case protocol::PacketType::debug:
                if (irqDebugChangedSinceLastLoop) return false; // defer packet
                memcpy(&irqDebugPacket, packet->buf, sizeof(irqDebugPacket));
                irqDebugChangedSinceLastLoop = true;
                break;
        }
        usb_free(packet);
    } else {
        // Handle frame buffer image data.
        if (irqReceivedNewFrameSinceLastLoop) return false; // defer packet
        // Take ownership of the packet.
        if (rendererHolder->get()->storeFramePacket(static_cast<uint8_t>(header), packet, len)) {
            irqReceivedNewFrameSinceLastLoop = true;
        }
    }

    irqHandledUsbPacketsSinceLastLoop = true;
    return true;
}

void dumpBool(const char* label, bool value) {
    serial_print("- ");
    serial_print(label);
    serial_print(": ");
    serial_print(value ? "true" : "false");
    serial_print("\r\n");
}

void dumpUnsigned(const char* label, unsigned value) {
    serial_print("- ");
    serial_print(label);
    serial_print(": ");
    serial_pdec32(value);
    serial_print("\r\n");
}

void dumpConfigPacket(const protocol::ConfigPacket& p) {
    serial_print("config packet:\r\n");
    dumpUnsigned("ledStrips", p.ledStrips);
    dumpUnsigned("ledsPerStrip", p.ledsPerStrip);
    dumpUnsigned("maxDitherBits", p.maxDitherBits);
    dumpUnsigned("colorFormat", static_cast<unsigned>(p.colorFormat));
    dumpUnsigned("ditherMode", static_cast<unsigned>(p.ditherMode));
    dumpUnsigned("interpolateMode", static_cast<unsigned>(p.interpolateMode));
    dumpUnsigned("indicatorMode", static_cast<unsigned>(p.indicatorMode));
    dumpUnsigned("timings.frequency", p.timings.frequency);
    dumpUnsigned("timings.resetInterval", p.timings.resetInterval);
    dumpUnsigned("timings.t0h", p.timings.t0h);
    dumpUnsigned("timings.t1h", p.timings.t1h);
}

void dumpDebugPacket(const protocol::DebugPacket& p) {
    serial_print("debug packet:\r\n");
    dumpBool("printStats", p.printStats);
}

void configure(render::RendererId id, render::RendererOptions options, const led::Timings& timings) {
    if (!rendererHolder->init(id, std::move(options))) {
        serial_print("invalid configuration: can't init renderer\r\n");
        return;
    }
    if (!led::init(options.ledsPerStrip, timings)) {
        rendererHolder->clear();
        serial_print("invalid configuration: can't init led driver\r\n");
    }
}

void setup() {
    // Announce firmware version
    serial_begin(BAUD2DIV(115200));
    serial_print("\r\nGlimmer v" CONFIG_DEVICE_VER_STRING "\r\n");

    // Configure peripherals.
    pinMode(LED_BUILTIN, OUTPUT);

    // Initialize globals and default parameters.
    frontOutputBuffer = outputBuffers[0];
    backOutputBuffer = outputBuffers[1];
    rendererHolder = new (&rendererHolderStorage) render::RendererHolder();
    irqConfigChangedSinceLastLoop = false;
    irqDebugChangedSinceLastLoop = false;
    irqHandledUsbPacketsSinceLastLoop = false;
    irqReceivedNewFrameSinceLastLoop = false;
    paramIndicatorMode = protocol::IndicatorMode::ACTIVITY;
    paramPrintStats = false;
    stats = {};
}

void loop() {
    // Render the next output buffer and write it out using DMA.
    if (rendererHolder->get()->render(backOutputBuffer)) {
        std::swap(frontOutputBuffer, backOutputBuffer);
        led::write(frontOutputBuffer);

        stats.renderedFrameCount++;
    }

    // Synchronize with the interrupt handler.
    // Flip buffers if a new frame was received.
    bool needUsbResume = false;
    if (irqReceivedNewFrameSinceLastLoop) {
        rendererHolder->get()->advanceFrame();

        irqReceivedNewFrameSinceLastLoop = false;
        stats.receivedFrameCount++;
        perf_receivedKeyframeCounter++;
        needUsbResume = true;
    }

    // Handle new debug settings.
    if (irqDebugChangedSinceLastLoop) {
        dumpDebugPacket(irqDebugPacket);

        paramPrintStats = irqDebugPacket.printStats;

        irqDebugChangedSinceLastLoop = false;
        needUsbResume = true;
    }

    // Handle new configuration settings.
    if (irqConfigChangedSinceLastLoop) {
        dumpConfigPacket(irqConfigPacket);

        paramIndicatorMode = irqConfigPacket.indicatorMode;
        render::RendererId id = {
            irqConfigPacket.colorFormat,
            irqConfigPacket.ditherMode,
            irqConfigPacket.interpolateMode
        };
        render::RendererOptions options = {
            irqConfigPacket.ledStrips,
            irqConfigPacket.ledsPerStrip,
            irqConfigPacket.maxDitherBits
        };

        configure(std::move(id), std::move(options), irqConfigPacket.timings);
        irqConfigChangedSinceLastLoop = false;
        needUsbResume = true;
    }

    if (needUsbResume) usb_rx_resume();

    // Update the activity LED activity.
    switch (paramIndicatorMode) {
        case protocol::IndicatorMode::OFF:
            digitalWriteFast(LED_BUILTIN, false);
            break;
        case protocol::IndicatorMode::ON:
            digitalWriteFast(LED_BUILTIN, true);
            break;
        case protocol::IndicatorMode::ACTIVITY:
        default:
            digitalWriteFast(LED_BUILTIN, irqHandledUsbPacketsSinceLastLoop);
            break;
    }
    irqHandledUsbPacketsSinceLastLoop = false;

    // Performance counter, for monitoring frame rate externally
    perf_frameCounter++;

    // Update statistics
    uint64_t now = micros64();
    if (now - stats.startTime > 10000000) {
        if (paramPrintStats) {
            serial_print("frames received: ");
            serial_pdec32(stats.receivedFrameCount);
            serial_print(", frames rendered: ");
            serial_pdec32(stats.renderedFrameCount);
            serial_print(" (during last 10 seconds)\r\n");
        }
        stats.startTime = now;
        stats.receivedFrameCount = 0;
        stats.renderedFrameCount = 0;
    }
}

} // namespace glimmer

// USB packet interrupt handler. Invoked by the ISR dispatch code in usb_dev.c
extern "C" int usb_rx_handler(usb_packet_t *packet, size_t len) {
    return glimmer::handleUsbRxIrq(packet, len);
}

// Reserved RAM area for signalling entry to bootloader
extern uint32_t boot_token;

extern "C" int main() {
    initSysticks();

    // Run application until asked to reboot into the bootloader
    glimmer::setup();
    while (usb_dfu_state == DFU_appIDLE) {
        watchdog_refresh();
        glimmer::loop();
    }

    // Reboot to the Fadecandy Bootloader
    boot_token = 0x74624346;

    // Short delay to allow the host to receive the response to DFU_DETACH.
    uint64_t deadline = millis64() + 10;
    while (millis64() < deadline) {
        watchdog_refresh();
    }

    // Detach from USB, and use the watchdog to time out a 10ms USB disconnect.
    __disable_irq();
    USB0_CONTROL = 0;
    crash("DFU entry");
}
