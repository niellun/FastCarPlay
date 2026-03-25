#ifndef SRC_PROTOCOL_CONNECTION
#define SRC_PROTOCOL_CONNECTION

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <thread>
#include <vector>

#include "struct/atomic_queue.h"
#include "protocol/aes_cipher.h"
#include "protocol/usb_buffer.h"
#include "recorder.h"

#define LINK_RETRY 3
#define CONNECT_RETRY 20
#define LINK_RETRY_TIMEOUT 100
#define RECONNECT_TIMEOUT 200
#define PROTOCOL_HEARTBEAT_DELAY 3000

#define WRITE_QUEUE_SIZE 128
#define VIDEO_QUEUE_SIZE 128
#define AUDIO_QUEUE_SIZE 128
#define PROCESS_QUEUE_SIZE 128

#define ENCRYPTION_BASE "SkBRDy3gmrw1ieH0"

class Connection
{

public:
    Connection();
    virtual ~Connection();

    void start();
    void stop();

    bool inline send(std::unique_ptr<Message> message) { return writeQueue.pushDiscard(std::move(message)); }
    uint32_t transfered() const { return _transfered.load(std::memory_order_acquire); }

    int8_t state() const { return _state.load(); }
    std::string connectionMethod() const { return _method; }
    std::string phoneName() const { return _phoneName; }
    const std::string status() const;

    AtomicQueue<Message> writeQueue;
    AtomicQueue<Message> videoStream;
    AtomicQueue<Message> audioStreamMain;
    AtomicQueue<Message> audioStreamAux;

private:
    struct Context
    {
        Connection *owner = nullptr;
        DataSlot *slot = nullptr;
        libusb_transfer *transfer = nullptr;
    };

    static void onTransfer(libusb_transfer *transfer);
    void mainLoop();
    void readLoop();
    void processLoop();
    void writeLoop(libusb_device_handle *handler, uint8_t ep);
    libusb_device *link(libusb_device_handle *handler, uint8_t *epIn, uint8_t *epOut);
    void setEncryption(bool enabled);
    bool fail(int status, const char *msg);
    void sendInit();
    void onDeviceConnect(libusb_device_handle *handler, libusb_device *device, uint8_t endpointIn);
    void onDeviceDisconnect();
    void onPhoneConnect();
    void onPhoneDisconnect();
    void onMessage(std::unique_ptr<Message> message);

    std::thread _writeThread;
    std::thread _readThread;
    std::thread _processThread;

    Recorder _recorder;
    UsbBuffer _processQueue;
    std::vector<Context> _transfers;
    atomic<int8_t> *_statusHandler;
    AESCipher *_cipher;
    libusb_context *_context;

    std::atomic<bool> _active;
    std::atomic<bool> _connected;
    std::atomic<bool> _phoneConnected;
    std::atomic<bool> _ecnrypt;
    std::atomic<int8_t> _state;

    std::string _method;
    std::string _phoneName;
    std::atomic<uint32_t> _transfered;
};

#endif /* SRC_PROTOCOL_CONNECTION */
