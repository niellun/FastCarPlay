#ifndef SRC_CONNECTOR
#define SRC_CONNECTOR

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>

#include "helper/isender.h"
#include "aes_cipher.h"
#include "struct/atomic_queue.h"
#include "struct/command.h"

#define READ_TIMEOUT 10000
#define MAX_USB_REQUESTS 64
#define COMMAND_QUEUE_SIZE 256
#define ENCRYPTION_BASE "SkBRDy3gmrw1ieH0"

#define PROTOCOL_DEBUG_NONE 0
#define PROTOCOL_DEBUG_UNKNOWN 1
#define PROTOCOL_DEBUG_NOSTREAM 2
#define PROTOCOL_DEBUG_OUT 3
#define PROTOCOL_DEBUG_ALL 4


class Connector : public ISender
{

public:
    Connector();
    virtual ~Connector();

    void start();
    void stop();
    bool send(std::unique_ptr<Command> packet) override;

protected:
    virtual void onData(uint8_t *data, uint32_t length) = 0;
    virtual void onStatus(u_int8_t status) = 0;
    virtual void onDevice(bool connected) = 0;

    void setEncryption(bool enabled);

    static void printMessage(uint32_t cmd, uint32_t length, uint8_t *data, bool encrypted, bool out);
    static void printInts(uint8_t *data, uint32_t length, uint16_t max);
    static void printBytes(uint8_t *data, uint32_t length, uint16_t max);
    static const char *cmdString(int cmd);

    AESCipher *_cipher = nullptr;    

private:
    static void onUsbRead(libusb_transfer *transfer);

    void readLoop();
    void writeLoop();
    void onDisconnect();
    bool connect(uint16_t vendor_id, uint16_t product_id);
    bool link();

    bool state(u_int8_t state);
    bool linkFail(int status, const char *msg);
    int write(int cmd, bool encrypt, uint8_t *data, uint32_t size);

    libusb_context *_context = nullptr;
    libusb_device_handle *_device = nullptr;
    uint8_t _endpoint_in;
    uint8_t _endpoint_out;
    uint8_t _usbTransfers;
    std::atomic<bool> _connected = false;
    std::atomic<bool> _ecnrypt = false;

    uint8_t _state;
    uint8_t _failCount;
    uint8_t _nodeviceCount;

    std::thread _read_thread;
    std::thread _write_thread;
    std::mutex _write_mutex;
    std::atomic<bool> _active = false;
    AtomicQueue<Command> _queue{COMMAND_QUEUE_SIZE};
    libusb_transfer *_usbTransfer[MAX_USB_REQUESTS] = {};
};

#endif /* SRC_CONNECTOR */
