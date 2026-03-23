#ifndef SRC_CONNECTOR
#define SRC_CONNECTOR

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>

#include "protocol/imessage_sender.h"
#include "struct/atomic_queue.h"
#include "struct/usb_buffer.h"
#include "protocol/aes_cipher.h"
#include "protocol/connection_reader.h"
#include "recorder.h"

#define LINK_RETRY 5
#define LINK_RETRY_TIMEOUT 100
#define RECONNECT_TIMEOUT 100
#define PROTOCOL_HEARTBEAT_DELAY 3000

#define WRITE_QUEUE_SIZE 256
#define ENCRYPTION_BASE "SkBRDy3gmrw1ieH0"

class Connection : public IMessageReceiver
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

    virtual void onMessage(std::unique_ptr<Message> message) override;

private:
    void mainLoop();
    void writeLoop(libusb_device_handle *handler, uint8_t ep);
    libusb_device *link(libusb_device_handle *handler, uint8_t *epIn, uint8_t *epOut);
    void setEncryption(bool enabled);
    bool fail(int status, const char *msg);
    bool state(u_int8_t state);
    void onDeviceConnect();
    void onDeviceDisconnect();

    Recorder _recorder;
    AESCipher *_cipher;
    ConnectionReader _reader;
    libusb_context *_context;
    std::thread _thread;

    std::atomic<bool> _active;
    std::atomic<bool> _connected;
    std::atomic<bool> _ecnrypt;

    uint8_t _state;
    uint8_t _failCount;
    uint8_t _nodeviceCount;

    atomic<int8_t> *_statusHandler;
};

#endif /* SRC_CONNECTOR */
