#ifndef SRC_PROTOCOL_CONNECTION_READER
#define SRC_PROTOCOL_CONNECTION_READER

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "struct/usb_buffer.h"
#include "protocol/aes_cipher.h"
#include "protocol/message.h"

class IMessageReceiver
{
public:
    virtual ~IMessageReceiver() = default;
    virtual void onMessage(std::unique_ptr<Message> message) = 0;
};

class ConnectionReader
{
public:
    ConnectionReader();
    ~ConnectionReader();

    ConnectionReader(const ConnectionReader &) = delete;
    ConnectionReader &operator=(const ConnectionReader &) = delete;

    bool start(libusb_context *context, libusb_device_handle *device, uint8_t endpoint, IMessageReceiver *receiver);
    void stop();

    int bufferCount() const { return _buffer.count(); }
    bool active() const { return _active; }

private:
    struct Context
    {
        ConnectionReader *owner = nullptr;
        DataSlot *slot = nullptr;
        libusb_transfer *transfer = nullptr;
    };

    static void onUsbRead(libusb_transfer *transfer);
    void readLoop();
    void processLoop();

    void cancelTransfers();

    std::atomic<bool> _active;
    UsbBuffer _buffer;
    std::vector<Context> _transfers;
    IMessageReceiver *_receiver;
    std::thread _readThread;
    std::thread _processThread;
    libusb_context *_usbContext;
};

#endif /* SRC_PROTOCOL_CONNECTION_READER */
