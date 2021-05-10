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

#pragma once
#include "usbdevice.h"
#include "opc.h"
#include <set>

#include "glimmer/protocol.h"

class GlimmerDevice : public USBDevice
{
public:
    GlimmerDevice(libusb_device *device, bool verbose);
    virtual ~GlimmerDevice();

    static bool probe(libusb_device *device);

    virtual int open();
    virtual void loadConfiguration(const Value &config);
    virtual void writeMessage(const OPC::Message &msg);
    virtual void writeMessage(Document &msg);
    virtual void writeColorCorrection(const Value &color);
    virtual std::string getName();
    virtual void flush();
    virtual void describe(rapidjson::Value &object, Allocator &alloc);

private:
    static const unsigned OUT_ENDPOINT = 1;
    static const unsigned MAX_FRAMES_PENDING = 2;

    enum PacketType {
        OTHER = 0,
        FRAME,
    };

    struct Transfer {
        Transfer(GlimmerDevice *device, void *buffer, int length, PacketType type = OTHER);
        ~Transfer();
        libusb_transfer *transfer;
        #if NEED_COPY_USB_TRANSFER_BUFFER
          void *bufferCopy;
        #endif
        PacketType type;
        bool finished;
    };

    const Value *mConfigMap;
    std::set<Transfer*> mPending;
    int mNumFramesPending;
    bool mFrameWaitingForSubmit;

    char mSerialBuffer[256];
    char mVersionString[10];

    libusb_device_descriptor mDD;

    bool mConfigInitialized = false;
    size_t mConfigFramePixelCount;
    size_t mConfigFramePacketCount;
    glimmer::protocol::ConfigPacket mConfigPacket = glimmer::protocol::configPacketDefault;
    glimmer::protocol::DebugPacket mDebugPacket = glimmer::protocol::debugPacketDefault;

    bool mFrameInitialized = false;
    glimmer::protocol::FramePacket mFramePackets[glimmer::protocol::maxPacketsPerFrame];

    bool mColorMapInitialized = false;
    uint16_t mColorMap[3][256];

    // Firmware configuration.
    void parseConfiguration(const Value &json);
    void writeConfiguration();

    // Send current buffer contents
    void clearFrame();
    void writeFrame();

    inline void writeDevicePixel24(size_t n, unsigned r, unsigned g, unsigned b) {
        const size_t ppp = glimmer::protocol::pixelsPerPacket(glimmer::protocol::ColorFormat::R8G8B8);
        glimmer::protocol::FramePacket* packet = &mFramePackets[n / ppp];
        size_t pixelIndex = n % ppp;
        uint8_t* pixel = &packet->data[pixelIndex * 3];
        pixel[0] = r;
        pixel[1] = g;
        pixel[2] = b;
    }

    inline void writeDevicePixel33(size_t n, unsigned r, unsigned g, unsigned b) {
        const size_t ppp = glimmer::protocol::pixelsPerPacket(glimmer::protocol::ColorFormat::R11G11B11);
        glimmer::protocol::FramePacket* packet = &mFramePackets[n / ppp];
        size_t pixelIndex = n % ppp;
        uint8_t* data = reinterpret_cast<uint8_t*>(packet);
        uint32_t* pixel = &reinterpret_cast<uint32_t*>(data + 4)[pixelIndex];
        uint16_t* blues = reinterpret_cast<uint16_t*>(data + 2);
        *pixel = (r << 21) | (g << 10) | (b >> 1);
        *blues = (*blues & ~(1u << pixelIndex)) | ((b & 1) << pixelIndex);
    }

    inline void writeDevicePixel(size_t n, unsigned r, unsigned g, unsigned b) {
        switch (mConfigPacket.colorFormat) {
            case glimmer::protocol::ColorFormat::R8G8B8:
                writeDevicePixel24(n, r, g, b);
                break;
            case glimmer::protocol::ColorFormat::R11G11B11:
                writeDevicePixel33(n, r, g, b);
                break;
        }
    }

    inline unsigned clamp(int x, unsigned max) {
        return x < 0 ? 0u : x > max ? max : static_cast<unsigned>(x);
    }

    inline void writeDevicePixelWithClamping(size_t n, int r, int g, int b) {
        switch (mConfigPacket.colorFormat) {
            case glimmer::protocol::ColorFormat::R8G8B8:
                writeDevicePixel24(n, clamp(r, 255u), clamp(g, 255u), clamp(b, 255u));
                break;
            case glimmer::protocol::ColorFormat::R11G11B11:
                writeDevicePixel33(n, clamp(r, 1023u), clamp(g, 1023u), clamp(b, 1023u));
                break;
        }
    }

    inline void writeColorMappedPixel(size_t n, unsigned r, unsigned g, unsigned b) {
        switch (mConfigPacket.colorFormat) {
            case glimmer::protocol::ColorFormat::R8G8B8:
                writeDevicePixel24(n, mColorMap[0][r] >> 8, mColorMap[1][g] >> 8, mColorMap[2][b] >> 8);
                break;
            case glimmer::protocol::ColorFormat::R11G11B11:
                writeDevicePixel33(n, mColorMap[0][r] >> 5, mColorMap[1][g] >> 5, mColorMap[2][b] >> 5);
                break;
        }
    }

    bool submitTransfer(Transfer *fct);
    void writeDevicePixels(Document &msg);
    static LIBUSB_CALL void completeTransfer(libusb_transfer *transfer);

    void opcSetPixelColors(const OPC::Message &msg);
    void opcSysEx(const OPC::Message &msg);
    void opcSetGlobalColorCorrection(const OPC::Message &msg);
    void opcSetFirmwareConfiguration(const OPC::Message &msg);
    void opcMapPixelColors(const OPC::Message &msg, const Value &inst);
};
