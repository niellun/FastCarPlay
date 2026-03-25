#ifndef SRC_PROTOCOL_CONNECTION
#define SRC_PROTOCOL_CONNECTION

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <thread>

#include "struct/atomic_queue.h"
#include "protocol/aes_cipher.h"
#include "recorder.h"

#define LINK_RETRY 5
#define LINK_RETRY_TIMEOUT 100
#define RECONNECT_TIMEOUT 100
#define PROTOCOL_HEARTBEAT_DELAY 3000
#define READ_TIMEOUT 1000

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

    void start(atomic<int8_t> *statusHandler);
    void stop();

    bool inline send(std::unique_ptr<Message> message) { return writeQueue.pushDiscard(std::move(message)); }

    AtomicQueue<Message> writeQueue;
    AtomicQueue<Message> videoStream;
    AtomicQueue<Message> audioStreamMain;
    AtomicQueue<Message> audioStreamAux;

private:
    void mainLoop();
    void readLoop();
    void processLoop();
    void writeLoop(libusb_device_handle *handler, uint8_t ep);
    libusb_device *link(libusb_device_handle *handler, uint8_t *epIn, uint8_t *epOut);
    void setEncryption(bool enabled);
    bool fail(int status, const char *msg);
    bool state(u_int8_t state);
    void sendInit();
    void onDeviceConnect(libusb_device_handle *handler, libusb_device *device, uint8_t endpointIn);
    void onDeviceDisconnect();
    void onPhoneConnect();
    void onPhoneDisconnect();

    std::thread _writeThread;
    std::thread _readThread;
    std::thread _processThread;

    Recorder _recorder;
    AtomicQueue<Message> _processQueue;
    atomic<int8_t> *_statusHandler;
    AESCipher *_cipher;
    libusb_context *_context;
    libusb_device_handle *_handler;
    libusb_device *_device;
    uint8_t _endpointIn;

    std::atomic<bool> _active;
    std::atomic<bool> _connected;
    std::atomic<bool> _phoneConnected;
    std::atomic<bool> _ecnrypt;

    uint8_t _state;
    uint8_t _failCount;
    uint8_t _nodeviceCount;
};

#endif /* SRC_PROTOCOL_CONNECTION */
