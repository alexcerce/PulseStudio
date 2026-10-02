// libusb side of the Push 2 display driver (kept out of the JUCE headers: libusb pulls in <windows.h>).
#include <JuceHeader.h>
#include <libusb.h>

namespace Push2Usb
{
    struct Device
    {
        libusb_context* ctx = nullptr;
        libusb_device_handle* handle = nullptr;
    };

    Device* open (juce::String& errorOut)
    {
        constexpr uint16_t vendorID = 0x2982, productID = 0x1967;   // Ableton Push 2

        auto d = std::make_unique<Device>();
        if (libusb_init (&d->ctx) != 0)
        {
            errorOut = "display: USB unavailable";
            return nullptr;
        }

        d->handle = libusb_open_device_with_vid_pid (d->ctx, vendorID, productID);
        if (d->handle == nullptr)
        {
            errorOut = "display: not found (needs the Push 2 USB driver)";
            libusb_exit (d->ctx);
            return nullptr;
        }

        if (libusb_claim_interface (d->handle, 0) != 0)
        {
            errorOut = "display: busy (is another app using Push?)";
            libusb_close (d->handle);
            libusb_exit (d->ctx);
            return nullptr;
        }

        return d.release();
    }

    void close (Device* d)
    {
        if (d == nullptr) return;
        if (d->handle != nullptr)
        {
            libusb_release_interface (d->handle, 0);
            libusb_close (d->handle);
        }
        if (d->ctx != nullptr) libusb_exit (d->ctx);
        delete d;
    }

    bool send (Device* d, const juce::uint8* data, int size)
    {
        int done = 0;
        return d != nullptr
            && libusb_bulk_transfer (d->handle, 0x01, const_cast<unsigned char*> (data), size, &done, 1000) == 0
            && done == size;
    }
}
