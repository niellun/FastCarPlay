#ifndef SRC_CONNECTOR
#define SRC_CONNECTOR

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>

#include "helper/iprotocol.h"

#define READ_TIMEOUT 5000

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint32_t length;
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

    int send(int cmd, uint8_t *data, uint32_t size);
    int send(int cmd);

    static void write_uint32_le(uint8_t *dst, uint32_t value);

private:
    void read_loop();
    void write_loop();

    bool connect(uint16_t vendor_id, uint16_t product_id);
    bool link();
    void release();
    
    void status(const char* status);

    libusb_context *_context = nullptr;
    libusb_device_handle *_device = nullptr;
    uint8_t _endpoint_in;
    uint8_t _endpoint_out;
    bool _connected;

    std::thread _read_thread;
    std::thread _write_thread;
    std::mutex _write_mutex;
    std::atomic<bool> _active = false;

    u_int16_t _videoPadding;

    IProtocol*  _protocol = nullptr;
};

#endif /* SRC_CONNECTOR */
