#ifndef SRC_CONNECTOR
#define SRC_CONNECTOR

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>

#include "helper/iprotocol.h"
#include "aes_cipher.h"

#define READ_TIMEOUT 3000
#define ENCRYPTION_BASE "SkBRDy3gmrw1ieH0"

#define PROTOCOL_DEBUG_NONE 0
#define PROTOCOL_DEBUG_UNKNOWN 1
#define PROTOCOL_DEBUG_NOSTREAM 2
#define PROTOCOL_DEBUG_OUT 3
#define PROTOCOL_DEBUG_ALL 4

#pragma pack(push, 1)
struct Header
{
    uint32_t magic;
    int32_t length;
    uint32_t type;
    uint32_t typecheck;
};
#pragma pack(pop)

class Connector
{

public:
    Connector(uint16_t videoPadding);
    ~Connector();

    void start(IProtocol *protocol);
    void stop();

    int send(int cmd, bool encrypt = true, uint8_t *data = nullptr, uint32_t size = 0);
    void setEncryption(bool enabled);

    AESCipher *Cypher() const { return _cipher; };

    static void printMessage(uint32_t cmd, uint32_t length, uint8_t *data, bool encrypted, bool out);
    static void printInts(uint8_t *data, uint32_t length, uint16_t max);
    static void printBytes(uint8_t *data, uint32_t length, uint16_t max);
    static const char *cmdString(int cmd);

private:
    void read_loop();
    void write_loop();

    bool connect(uint16_t vendor_id, uint16_t product_id);
    bool link();
    void release();

    void state(u_int8_t state);
    bool nextState(u_int8_t state);
    bool linkFail(int status, const char *msg);

    libusb_context *_context = nullptr;
    libusb_device_handle *_device = nullptr;
    uint8_t _endpoint_in;
    uint8_t _endpoint_out;
    bool _connected;
    std::atomic<bool> _ecnrypt = false;

    uint8_t _state;
    uint8_t _failCount;
    uint8_t _nodeviceCount;
    std::string _lastError;

    std::thread _read_thread;
    std::thread _write_thread;
    std::mutex _write_mutex;
    std::atomic<bool> _active = false;

    u_int16_t _videoPadding;

    IProtocol *_protocol = nullptr;
    AESCipher *_cipher = nullptr;
};

#endif /* SRC_CONNECTOR */
