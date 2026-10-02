#pragma once

#include "Common.h"

/** Thin libusb wrapper, compiled separately (Push2Usb.cpp) so <windows.h> stays out of the headers. */
namespace Push2Usb
{
    struct Device;
    Device* open (String& errorOut);          // nullptr on failure
    void close (Device*);
    bool send (Device*, const uint8* data, int size);   // bulk OUT endpoint 0x01
}

/** Drives the Ableton Push 2's 960x160 display over USB (libusb, WinUSB driver).
    Protocol from Ableton's public "Push 2 MIDI and Display Interface Manual":
    a 16-byte frame header, then 160 lines of 960 BGR565 pixels (+128 filler bytes per
    line), XOR-ed with a signal-shaping pattern, sent to bulk endpoint 0x01. */
class Push2Display : private Thread
{
public:
    static constexpr int width = 960, height = 160;

    Push2Display() : Thread ("Push 2 display") { startThread(); }

    ~Push2Display() override
    {
        signalThreadShouldExit();
        newFrame.signal();
        stopThread (3000);
        close();
    }

    /** True once the display interface is open and frames are being sent. */
    bool isOpen() const { return open.load(); }
    String getStatus() const { const ScopedLock sl (statusLock); return status; }

    /** Converts and queues a frame (call from any thread, typically the message thread). */
    void setFrame (const Image& image)
    {
        auto buf = encode (image);
        {
            const ScopedLock sl (frameLock);
            pending = std::move (buf);
        }
        newFrame.signal();
    }

    /** Image -> the bytes the Push expects (BGR565, 2048-byte lines, XOR-shaped). */
    static std::vector<uint8> encode (const Image& image)
    {
        jassert (image.getWidth() == width && image.getHeight() == height);
        std::vector<uint8> buf ((size_t) lineBytes * height);
        const Image::BitmapData bd (image, Image::BitmapData::readOnly);
        static const uint8 mask[4] = { 0xE7, 0xF3, 0xE7, 0xFF };

        for (int y = 0; y < height; ++y)
        {
            auto* line = buf.data() + (size_t) y * lineBytes;
            for (int x = 0; x < width; ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                const uint16 px = (uint16) (((c.getBlue() >> 3) << 11) | ((c.getGreen() >> 2) << 5) | (c.getRed() >> 3));
                line[x * 2]     = (uint8) (px & 0xFF);
                line[x * 2 + 1] = (uint8) (px >> 8);
            }
            for (int i = 0; i < lineBytes; ++i)
                line[i] ^= mask[i & 3];
        }
        return buf;
    }

    static constexpr int lineBytes = 2048;

private:

    void setStatus (const String& s) { const ScopedLock sl (statusLock); status = s; }

    bool tryOpen()
    {
        String err;
        device = Push2Usb::open (err);
        if (device == nullptr) { setStatus (err); return false; }
        open = true;
        setStatus ("display on");
        return true;
    }

    void close()
    {
        if (device != nullptr) Push2Usb::close (device);
        device = nullptr;
        open = false;
    }

    bool send (const std::vector<uint8>& frame)
    {
        static const uint8 header[16] = { 0xFF, 0xCC, 0xAA, 0x88 };
        if (! Push2Usb::send (device, header, 16))
            return false;

        constexpr int chunk = 16384;
        for (size_t pos = 0; pos < frame.size(); pos += chunk)
            if (! Push2Usb::send (device, frame.data() + pos, (int) jmin ((size_t) chunk, frame.size() - pos)))
                return false;
        return true;
    }

    void run() override
    {
        uint32 lastAttempt = 0;
        std::vector<uint8> frame;

        while (! threadShouldExit())
        {
            if (device == nullptr)
            {
                // the display needs a frame at least every 2 s, so re-try every couple of seconds
                if (Time::getMillisecondCounter() - lastAttempt > 2000)
                {
                    lastAttempt = Time::getMillisecondCounter();
                    tryOpen();
                }
                if (device == nullptr) { wait (250); continue; }
            }

            newFrame.wait (500);   // resend the last frame at least twice a second
            {
                const ScopedLock sl (frameLock);
                if (! pending.empty()) frame.swap (pending), pending.clear();
            }
            if (frame.empty() || threadShouldExit())
                continue;

            if (! send (frame))
            {
                close();
                setStatus ("display: disconnected");
            }
        }
    }

    Push2Usb::Device* device = nullptr;
    std::atomic<bool> open { false };
    CriticalSection frameLock, statusLock;
    std::vector<uint8> pending;
    WaitableEvent newFrame;
    String status { "display: starting" };
};
