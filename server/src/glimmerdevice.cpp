/*
 * Fadecandy device interface for Glimmer firmware
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

#include "glimmerdevice.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "opc.h"
#include <math.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <stdio.h>


GlimmerDevice::Transfer::Transfer(GlimmerDevice *device, void *buffer, int length, PacketType type)
    : transfer(libusb_alloc_transfer(0)),
      type(type), finished(false)
{
    #if NEED_COPY_USB_TRANSFER_BUFFER
        bufferCopy = malloc(length);
        memcpy(bufferCopy, buffer, length);
        uint8_t *data = (uint8_t*) bufferCopy;
    #else
        uint8_t *data = (uint8_t*) buffer;
    #endif

    libusb_fill_bulk_transfer(transfer, device->mHandle,
        OUT_ENDPOINT, data, length, GlimmerDevice::completeTransfer, this, 2000);
}

GlimmerDevice::Transfer::~Transfer()
{
    libusb_free_transfer(transfer);
    #if NEED_COPY_USB_TRANSFER_BUFFER
        free(bufferCopy);
    #endif
}

GlimmerDevice::GlimmerDevice(libusb_device *device, bool verbose)
    : USBDevice(device, "glimmer", verbose),
      mConfigMap(0), mNumFramesPending(0), mFrameWaitingForSubmit(false)
{
    mSerialBuffer[0] = '\0';
    mSerialString = mSerialBuffer;
}

GlimmerDevice::~GlimmerDevice()
{
    /*
     * If we have pending transfers, cancel them.
     * The Transfer objects themselves will be freed
     * once libusb completes them.
     */

    for (std::set<Transfer*>::iterator i = mPending.begin(), e = mPending.end(); i != e; ++i) {
        Transfer *fct = *i;
        libusb_cancel_transfer(fct->transfer);
    }
}

bool GlimmerDevice::probe(libusb_device *device)
{
    libusb_device_descriptor dd;

    if (libusb_get_device_descriptor(device, &dd) < 0) {
        // Can't access descriptor?
        return false;
    }

    return dd.idVendor == 0x1d50 && dd.idProduct == 0x607a && dd.bcdDevice >= 0x0390;
}

int GlimmerDevice::open()
{
    int r = libusb_get_device_descriptor(mDevice, &mDD);
    if (r < 0) {
        return r;
    }

    r = libusb_open(mDevice, &mHandle);
    if (r < 0) {
        return r;
    }

    r = libusb_claim_interface(mHandle, 0);
    if (r < 0) {
        return r;
    }

    unsigned major = mDD.bcdDevice >> 8;
    unsigned minor = mDD.bcdDevice & 0xFF;
    snprintf(mVersionString, sizeof mVersionString, "%x.%02x", major, minor);

    return libusb_get_string_descriptor_ascii(mHandle, mDD.iSerialNumber, 
        (uint8_t*)mSerialBuffer, sizeof mSerialBuffer);
}

void GlimmerDevice::completeTransfer(libusb_transfer *transfer)
{
    GlimmerDevice::Transfer *fct = static_cast<GlimmerDevice::Transfer*>(transfer->user_data);
    fct->finished = true;
}

void GlimmerDevice::flush()
{
    // Erase any finished transfers

    std::set<Transfer*>::iterator current = mPending.begin();
    while (current != mPending.end()) {
        std::set<Transfer*>::iterator next = current;
        next++;

        Transfer *fct = *current;
        if (fct->finished) {
            switch (fct->type) {

                case FRAME:
                    mNumFramesPending--;
                    break;

                default:
                    break;
            }

            mPending.erase(current);
            delete fct;
        }

        current = next;
    }

    // Submit new frames, if we had a queued frame waiting
    if (mFrameWaitingForSubmit && mNumFramesPending < MAX_FRAMES_PENDING) {
        writeFrame();
    }
}

void GlimmerDevice::loadConfiguration(const Value &config)
{
    mConfigMap = findConfigMap(config);

    // Initial firmware configuration from our device options
    parseConfiguration(config);
    writeConfiguration();
    clearFrame();
}

void GlimmerDevice::parseConfiguration(const Value &config)
{
    mConfigInitialized = true;
    mConfigPacket = glimmer::protocol::configPacketDefault;
    mDebugPacket = glimmer::protocol::debugPacketDefault;

    if (!config.IsObject()) {
        std::clog << "Configuration is not a JSON object\n";
        return; // assume default values
    }

    // Strips
    const Value &strips = config["strips"];
    if (strips.IsUint() && strips.GetUint() >= 1 && strips.GetUint() <= 8) {
        mConfigPacket.ledStrips = strips.GetUint();
    } else if (!strips.IsNull()) {
        std::clog << "Value for 'strips' must be 1 to 8, or null (default).";
    }

    // Strip Length
    const Value &stripLength = config["stripLength"];
    if (stripLength.IsUint() && stripLength.GetUint() >= 1 && stripLength.GetUint() <= 255) {
        mConfigPacket.ledsPerStrip = stripLength.GetUint();
    } else if (!stripLength.IsNull()) {
        std::clog << "Value for 'stripLength' must be 1 to 255, or null (default).";
    }

    // Indicator LED
    const Value &led = config["led"];
    if (led.IsBool()) {
        mConfigPacket.indicatorMode = led.IsTrue() ? glimmer::protocol::IndicatorMode::ON :
                glimmer::protocol::IndicatorMode::OFF;
    } else if (!led.IsNull()) {
        std::clog << "Value for 'led' must be true, false, or null (default).";
    }

    // Dithering
    const Value &dither = config["dither"];
    if (dither.IsBool()) {
        mConfigPacket.ditherMode = dither.IsTrue() ? glimmer::protocol::DitherMode::TEMPORAL :
                glimmer::protocol::DitherMode::NONE;
    } else if (!dither.IsNull()) {
        std::clog << "Value for 'dither' must be true, false, or null (default).";
    }
    const Value &ditherBits = config["ditherBits"];
    if (ditherBits.IsUint() && ditherBits.GetUint() <= 8) {
        mConfigPacket.maxDitherBits = ditherBits.GetUint();
    } else if (!ditherBits.IsNull()) {
        std::clog << "Value for 'ditherBits' must be 0 to 8, or null (default).";
    }

    // Interpolation
    const Value &interpolate = config["interpolate"];
    if (interpolate.IsBool()) {
        mConfigPacket.interpolateMode = interpolate.IsTrue() ? glimmer::protocol::InterpolateMode::LINEAR :
                glimmer::protocol::InterpolateMode::NONE;
    } else if (!interpolate.IsNull()) {
        std::clog << "Value for 'interpolate' must be true, false, or null (default).";
    }

    // Color depth
    const Value &colorDepth = config["colorDepth"];
    if (colorDepth.IsUint() && colorDepth.GetUint() == 24 || colorDepth.GetUint() == 33) {
        mConfigPacket.colorFormat = colorDepth.GetUint() == 24 ? glimmer::protocol::ColorFormat::R8G8B8 :
                glimmer::protocol::ColorFormat::R11G11B11;
    } else if (!colorDepth.IsNull()) {
        std::clog << "Value for 'colorDepth' must be 24 or 33, or null (default).";
    }

    // Check frame dimensions.
    for (;;) {
        mConfigFramePixelCount = static_cast<size_t>(mConfigPacket.ledStrips) * mConfigPacket.ledsPerStrip;
        mConfigFramePacketCount = glimmer::protocol::packetsPerFrame(
                mConfigPacket.ledStrips, mConfigPacket.ledsPerStrip, mConfigPacket.colorFormat);
        if (mConfigFramePacketCount <= glimmer::protocol::maxPacketsPerFrame) break;

        std::clog << "Product of 'strips' and 'stripLength' is too big, frame can have no more than "
                << glimmer::protocol::pixelsPerPacket(mConfigPacket.colorFormat) * glimmer::protocol::maxPacketsPerFrame
                << " pixels at the configured color depth.";
        mConfigPacket.ledStrips = glimmer::protocol::configPacketDefault.ledStrips;
        mConfigPacket.ledsPerStrip = glimmer::protocol::configPacketDefault.ledsPerStrip;
    }

    // Timings
    const Value &timings = config["timings"];
    if (timings.IsString() && glimmer::led::timingsByName(timings.GetString())) {
        mConfigPacket.timings = *glimmer::led::timingsByName(timings.GetString());
    } else if (timings.IsArray() && timings.Size() == 4 &&
            timings[0u].IsUint() && timings[1u].IsUint() &&
            timings[2u].IsUint() && timings[3u].IsUint()) {
        mConfigPacket.timings = glimmer::led::Timings {
            timings[0u].GetUint(), timings[1u].GetUint(),
            timings[2u].GetUint(), timings[3u].GetUint()
        };
    } else if (!timings.IsNull()) {
        std::clog << "Value for 'timings' must be one of [";
        for (const auto& elem : glimmer::led::namedTimings) {
            std::clog << '"' << elem.name << '"';
        }
        std::clog << "], an array of 4 integers, or null (default).";
    }

    // Debugging options.
    const Value& debug = config["debug"];
    if (debug.IsObject()) {
        const Value& printStats = debug["printStats"];
        if (printStats.IsBool()) {
            mDebugPacket.printStats = printStats.IsTrue();
        } else if (!printStats.IsNull()) {
            std::clog << "Value for 'printStats' must be true, false, or null (default).";
        }
    }
}

void GlimmerDevice::writeConfiguration()
{
    if (mConfigInitialized) {
        submitTransfer(new Transfer(this, &mConfigPacket, sizeof(mConfigPacket)));
        submitTransfer(new Transfer(this, &mDebugPacket, sizeof(mDebugPacket)));
    }
}

bool GlimmerDevice::submitTransfer(Transfer *fct)
{
    /*
     * Submit a new USB transfer. The Transfer object is guaranteed to be freed eventually.
     * On error, it's freed right away.
     */

    int r = libusb_submit_transfer(fct->transfer);

    if (r < 0) {
        if (mVerbose && r != LIBUSB_ERROR_PIPE) {
            std::clog << "Error submitting USB transfer: " << libusb_strerror(libusb_error(r)) << "\n";
        }
        delete fct;
        return false;

    } else {
        mPending.insert(fct);
        return true;
    }
}

void GlimmerDevice::writeColorCorrection(const Value &color)
{
    /*
     * Populate the color correction table based on a JSON configuration object,
     * and send the new color LUT out over USB.
     *
     * 'color' may be 'null' to load an identity-mapped LUT, or it may be
     * a dictionary of options including 'gamma' and 'whitepoint'.
     *
     * This calculates a compound curve with a linear section and a nonlinear
     * section. The linear section, near zero, avoids creating very low output
     * values that will cause distracting flicker when dithered. This isn't a problem
     * when the LEDs are viewed indirectly such that the flicker is below the threshold
     * of perception, but in cases where the flicker is a problem this linear section can
     * eliminate it entierly at the cost of some dynamic range.
     *
     * By default, the linear section is disabled (linearCutoff is zero). To enable the
     * linear section, set linearCutoff to some nonzero value. A good starting point is
     * 1/256.0, correspnding to the lowest 8-bit PWM level.
     */

    // Default color LUT parameters
    double gamma = 1.0;                         // Power for nonlinear portion of curve
    double whitepoint[3] = {1.0, 1.0, 1.0};     // White-point RGB value (also, global brightness)
    double linearSlope = 1.0;                   // Slope (output / input) of linear section of the curve, near zero
    double linearCutoff = 0.0;                  // Y (output) coordinate of intersection of linear and nonlinear curves

    /*
     * Parse the JSON object
     */

    if (color.IsObject()) {
        const Value &vGamma = color["gamma"];
        const Value &vWhitepoint = color["whitepoint"];
        const Value &vLinearSlope = color["linearSlope"];
        const Value &vLinearCutoff = color["linearCutoff"];

        if (vGamma.IsNumber()) {
            gamma = vGamma.GetDouble();
        } else if (!vGamma.IsNull() && mVerbose) {
            std::clog << "Gamma value must be a number.\n";
        }

        if (vLinearSlope.IsNumber()) {
            linearSlope = vLinearSlope.GetDouble();
        } else if (!vLinearSlope.IsNull() && mVerbose) {
            std::clog << "Linear slope value must be a number.\n";
        }

        if (vLinearCutoff.IsNumber()) {
            linearCutoff = vLinearCutoff.GetDouble();
        } else if (!vLinearCutoff.IsNull() && mVerbose) {
            std::clog << "Linear slope value must be a number.\n";
        }

        if (vWhitepoint.IsArray() &&
            vWhitepoint.Size() == 3 &&
            vWhitepoint[0u].IsNumber() &&
            vWhitepoint[1].IsNumber() &&
            vWhitepoint[2].IsNumber()) {
            whitepoint[0] = vWhitepoint[0u].GetDouble();
            whitepoint[1] = vWhitepoint[1].GetDouble();
            whitepoint[2] = vWhitepoint[2].GetDouble();
        } else if (!vWhitepoint.IsNull() && mVerbose) {
            std::clog << "Whitepoint value must be a list of 3 numbers.\n";
        }

    } else if (!color.IsNull() && mVerbose) {
        std::clog << "Color correction value must be a JSON dictionary object.\n";
    }

    /*
     * Calculate the color LUT, setting the result aside for color mapping.
     */

    for (unsigned channel = 0; channel < 3; channel++) {
        for (unsigned entry = 0; entry < 256; entry++) {
            double output;

            /*
             * Normalized input value corresponding to this LUT entry.
             * Ranges from 0 to 1.
             */
            double input = entry / 255.0;

            // Scale by whitepoint before anything else
            input *= whitepoint[channel];

            // Is this entry part of the linear section still?
            if (input * linearSlope <= linearCutoff) {

                // Output value is below linearCutoff. We're still in the linear portion of the curve
                output = input * linearSlope;

            } else {

                // Nonlinear portion of the curve. This starts right where the linear portion leaves
                // off. We need to avoid any discontinuity.

                double nonlinearInput = input - (linearSlope * linearCutoff);
                double scale = 1.0 - linearCutoff;
                output = linearCutoff + pow(nonlinearInput / scale, gamma) * scale;
            }

            // Generate the correct number of bits per color component for the frame buffer
            // to avoid overflows when dithering.
            output = std::min(std::max(output, 0.0), 1.0);
            switch (mConfigPacket.colorFormat) {
                case glimmer::protocol::ColorFormat::R8G8B8:
                    mColorMap[channel][entry] = uint16_t(output * 0xff);
                    break;
                case glimmer::protocol::ColorFormat::R11G11B11:
                    mColorMap[channel][entry] = uint16_t(output * 0x7f8);
                    break;
            }
        }
    }
    mColorMapInitialized = true;
}

void GlimmerDevice::clearFrame() {
    memset(&mFramePackets, 0, sizeof(mFramePackets));
    for (uint8_t i = 0; i < mConfigFramePacketCount; ++i) {
        mFramePackets[i].index = i;
    }
}

void GlimmerDevice::writeFrame()
{
    /*
     * Asynchronously write the current framebuffer.
     *
     * TODO: Currently if this gets ahead of what the USB device is capable of,
     *       we always drop frames. Alternatively, it would be nice to have end-to-end
     *       flow control so that the client can produce frames slower.
     */

    if (!mConfigInitialized) return;

    if (mNumFramesPending >= MAX_FRAMES_PENDING) {
        // Too many outstanding frames. Wait to submit until a previous frame completes.
        mFrameWaitingForSubmit = true;
        return;
    }

    if (submitTransfer(new Transfer(this, &mFramePackets,
            mConfigFramePacketCount * sizeof(mFramePackets[0]), FRAME))) {
        mFrameWaitingForSubmit = false;
        mNumFramesPending++;
    }
}

void GlimmerDevice::writeMessage(Document &msg)
{
    /*
     * Dispatch a device-specific JSON command.
     *
     * This can be used to send frames or settings directly to one device,
     * bypassing the mapping we use for Open Pixel Control clients. This isn't
     * intended to be the fast path for regular applications, but it can be used
     * by configuration tools that need to operate regardless of the mapping setup.
     */

    const char *type = msg["type"].GetString();

    if (!strcmp(type, "device_options")) {
        /*
         * TODO: Eventually this should turn into the same thing as 
         *       loadConfiguration() and it shouldn't be device-specific,
         *       but for now most of fcserver assumes the configuration is static.
         */
        parseConfiguration(msg["options"]);
        writeConfiguration();
        clearFrame();
        return;
    }

    if (!strcmp(type, "device_pixels")) {
        // Write raw pixels, without any mapping
        writeDevicePixels(msg);
        return;
    }

    // Chain to default handler
    USBDevice::writeMessage(msg);
}

void GlimmerDevice::writeDevicePixels(Document &msg)
{
    /*
     * Write pixels without mapping, from a JSON integer
     * array in msg["pixels"]. The pixel array is removed from
     * the reply to save network bandwidth.
     *
     * Color components are clamped based on the configured color depth:
     *   24-bits: [0, 255]
     *   33-bits: [0, 1023]
     */

    if (!mConfigInitialized) return;

    const Value &pixels = msg["pixels"];
    if (!pixels.IsArray()) {
        msg.AddMember("error", "Pixel array is missing", msg.GetAllocator());
    } else {
        size_t numPixels = std::min<size_t>(pixels.Size() / 3, mConfigFramePixelCount);
        for (size_t i = 0; i < numPixels; i++) {
            const Value &r = pixels[i * 3 + 0];
            const Value &g = pixels[i * 3 + 1];
            const Value &b = pixels[i * 3 + 2];
            writeDevicePixelWithClamping(i,
                    r.IsInt() ? r.GetInt() : 0,
                    g.IsInt() ? g.GetInt() : 0,
                    b.IsInt() ? b.GetInt() : 0);
        }

        writeFrame();
    }
}

void GlimmerDevice::writeMessage(const OPC::Message &msg)
{
    /*
     * Dispatch an incoming OPC command
     */

    switch (msg.command) {

        case OPC::SetPixelColors:
            opcSetPixelColors(msg);
            writeFrame();
            return;

        case OPC::SystemExclusive:
            opcSysEx(msg);
            return;
    }

    if (mVerbose) {
        std::clog << "Unsupported OPC command: " << unsigned(msg.command) << "\n";
    }
}

void GlimmerDevice::opcSysEx(const OPC::Message &msg)
{
    if (msg.length() < 4) {
        if (mVerbose) {
            std::clog << "SysEx message too short!\n";
        }
        return;
    }

    unsigned id = (unsigned(msg.data[0]) << 24) |
                  (unsigned(msg.data[1]) << 16) |
                  (unsigned(msg.data[2]) << 8)  |
                   unsigned(msg.data[3])        ;

    switch (id) {

        case OPC::FCSetGlobalColorCorrection:
            return opcSetGlobalColorCorrection(msg);

        case OPC::FCSetFirmwareConfiguration:
            return opcSetFirmwareConfiguration(msg);

    }

    // Quietly ignore unhandled SysEx messages.
}

void GlimmerDevice::opcSetPixelColors(const OPC::Message &msg)
{
    /*
     * Parse through our device's mapping, and store any relevant portions of 'msg'
     * in the framebuffer.
     */

    if (!mConfigMap || !mConfigInitialized || !mColorMapInitialized) {
        // No mapping defined yet. This device is inactive.
        return;
    }

    const Value &map = *mConfigMap;
    for (unsigned i = 0, e = map.Size(); i != e; i++) {
        opcMapPixelColors(msg, map[i]);
    }
}

void GlimmerDevice::opcMapPixelColors(const OPC::Message &msg, const Value &inst)
{
    /*
     * Parse one JSON mapping instruction, and copy any relevant parts of 'msg'
     * into our framebuffer. This looks for any mapping instructions that we
     * recognize:
     *
     *   [ OPC Channel, First OPC Pixel, First output pixel, Pixel count ]
     *   [ OPC Channel, First OPC Pixel, First output pixel, Color channels ]
     */

    unsigned msgPixelCount = msg.length() / 3;

    if (inst.IsArray() && inst.Size() == 4) {
        // Map a range from an OPC channel to our framebuffer

        const Value &vChannel = inst[0u];
        const Value &vFirstOPC = inst[1];
        const Value &vFirstOut = inst[2];
        const Value &vCount = inst[3];

        if (vChannel.IsUint() && vFirstOPC.IsUint() && vFirstOut.IsUint() && vCount.IsInt()) {
            unsigned channel = vChannel.GetUint();
            unsigned firstOPC = vFirstOPC.GetUint();
            unsigned firstOut = vFirstOut.GetUint();
            unsigned count;
            int direction;
            if (vCount.GetInt() >= 0) {
                count = vCount.GetInt();
                direction = 1;
            } else {
                count = -vCount.GetInt();
                direction = -1;
            }

            if (channel != msg.channel) {
                return;
            }

            // Clamping, overflow-safe
            firstOPC = std::min<unsigned>(firstOPC, msgPixelCount);
            firstOut = std::min<unsigned>(firstOut, mConfigFramePixelCount);
            count = std::min<unsigned>(count, msgPixelCount - firstOPC);
            count = std::min<unsigned>(count,
                    direction > 0 ? mConfigFramePixelCount - firstOut : firstOut + 1);

            // Copy pixels
            const uint8_t *inPtr = msg.data + (firstOPC * 3);
            unsigned outIndex = firstOut;
            while (count--) {
                writeColorMappedPixel(outIndex, inPtr[0], inPtr[1], inPtr[2]);
                outIndex += direction;
                inPtr += 3;
            }

            return;
        }
    }

    if (inst.IsArray() && inst.Size() == 5) {
        // Map a range from an OPC channel to our framebuffer, with color channel swizzling

        const Value &vChannel = inst[0u];
        const Value &vFirstOPC = inst[1];
        const Value &vFirstOut = inst[2];
        const Value &vCount = inst[3];
        const Value &vColorChannels = inst[4];

        if (vChannel.IsUint() && vFirstOPC.IsUint() && vFirstOut.IsUint() && vCount.IsInt()
            && vColorChannels.IsString() && vColorChannels.GetStringLength() == 3) {

            unsigned channel = vChannel.GetUint();
            unsigned firstOPC = vFirstOPC.GetUint();
            unsigned firstOut = vFirstOut.GetUint();
            unsigned count;
            int direction;
            if (vCount.GetInt() >= 0) {
                count = vCount.GetInt();
                direction = 1;
            } else {
                count = -vCount.GetInt();
                direction = -1;
            }
            const char *colorChannels = vColorChannels.GetString();

            if (channel != msg.channel) {
                return;
            }

            // Clamping, overflow-safe
            firstOPC = std::min<unsigned>(firstOPC, msgPixelCount);
            firstOut = std::min<unsigned>(firstOut, mConfigFramePixelCount);
            count = std::min<unsigned>(count, msgPixelCount - firstOPC);
            count = std::min<unsigned>(count,
                    direction > 0 ? mConfigFramePixelCount - firstOut : firstOut + 1);

            // Copy pixels
            const uint8_t *inPtr = msg.data + (firstOPC * 3);
            unsigned outIndex = firstOut;
            bool success = true;
            uint8_t color[3] = {};
            while (count--) {
                for (int channel = 0; channel < 3; channel++) {
                    if (!OPC::pickColorChannel(color[channel], colorChannels[channel], inPtr)) {
                        success = false;
                        break;
                    }
                }

                writeColorMappedPixel(outIndex, color[0], color[1], color[2]);
                outIndex += direction;
                inPtr += 3;
            }

            if (success) {
                return;
            }
        }
    }

    // Still haven't found a match?
    if (mVerbose) {
        rapidjson::GenericStringBuffer<rapidjson::UTF8<> > buffer;
        rapidjson::Writer<rapidjson::GenericStringBuffer<rapidjson::UTF8<> > > writer(buffer);
        inst.Accept(writer);
        std::clog << "Unsupported JSON mapping instruction: " << buffer.GetString() << "\n";
    }
}

void GlimmerDevice::opcSetGlobalColorCorrection(const OPC::Message &msg)
{
    /*
     * Parse the message as JSON text, and if successful, write new
     * color correction data to the device.
     */

    // Mutable NUL-terminated copy of the message string
    std::string text((char*)msg.data + 4, msg.length() - 4);

    // Parse it in-place
    rapidjson::Document doc;
    doc.ParseInsitu<0>(&text[0]);

    if (doc.HasParseError()) {
        if (mVerbose) {
            std::clog << "Parse error in color correction JSON at character "
                << doc.GetErrorOffset() << ": " << doc.GetParseError() << "\n";
        }
        return;
    }

    /*
     * Successfully parsed the JSON. From here, it's handled identically to
     * objects that come through the config file.
     */
    writeColorCorrection(doc);
}

void GlimmerDevice::opcSetFirmwareConfiguration(const OPC::Message &msg)
{
    // We longer support writing raw firmware configuration packets.
    // TODO: If this is in common usage, we could still parse the message and add
    //       backwards compatibility logic to produce similar effects.
}

std::string GlimmerDevice::getName()
{
    std::ostringstream s;
    s << "Glimmer";
    if (mSerialString[0]) {
        s << " (Serial# " << mSerialString << ", Version " << mVersionString << ")";
    }
    return s.str();
}

void GlimmerDevice::describe(rapidjson::Value &object, Allocator &alloc)
{
    USBDevice::describe(object, alloc);
    object.AddMember("version", mVersionString, alloc);
    object.AddMember("bcd_version", mDD.bcdDevice, alloc);
}
