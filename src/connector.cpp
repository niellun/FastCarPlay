#include "connector.h"

#include <stdexcept>
#include <iostream>
#include <condition_variable>

#include "helper/functions.h"
#include "settings.h"

Connector::Connector(uint16_t videoPadding)
    : _videoPadding(videoPadding)
{
    int result = libusb_init(&_context);
    if (result < 0)
        throw std::runtime_error(std::string("Can't initialise USB: ") + libusb_error_name(result));
}

Connector::~Connector()
{
    stop();

    if (_device)
    {
        libusb_release_interface(_device, 0);
        libusb_close(_device);
        _device = nullptr;
    }

    if (_context)
    {
        libusb_exit(_context);
        _context = nullptr;
    }
}

void Connector::start(IProtocol *protocol)
{
    _protocol = protocol;

    if (_active)
        stop();

    _active = true;
    _write_thread = std::thread(&Connector::write_loop, this);
    _read_thread = std::thread(&Connector::read_loop, this);
}

void Connector::stop()
{
    if (!_active)
        return;

    _active = false;
    if (_read_thread.joinable())
        _read_thread.join();

    if (_write_thread.joinable())
        _write_thread.join();
}

bool Connector::connect(uint16_t vendor_id, uint16_t product_id)
{
    status("Searching for device");

    std::cout << "Creating device handle" << std::endl;
    _device = libusb_open_device_with_vid_pid(_context, vendor_id, product_id);
    if (!_device)
    {
        status("Can't find device");
        return false;
    }

    if (link())
        return true;

    libusb_close(_device);
    _device = nullptr;

    return false;
}

bool Connector::link()
{
    status("Linking device");

    std::cout << "Reset device" << std::endl;
    if (libusb_reset_device(_device) < 0)
        return false;

    std::cout << "Set configuration" << std::endl;
    if (libusb_set_configuration(_device, 1) < 0)
        return false;

    std::cout << "Claim interface" << std::endl;
    if (libusb_claim_interface(_device, 0) < 0)
        return false;

    std::cout << "Get config descriptor" << std::endl;
    libusb_device *dev = libusb_get_device(_device);
    struct libusb_config_descriptor *config = nullptr;

    if (libusb_get_active_config_descriptor(dev, &config) < 0)
        return false;

    for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++)
    {
        const struct libusb_endpoint_descriptor *ep = &config->interface[0].altsetting[0].endpoint[i];
        if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
        {
            _endpoint_in = ep->bEndpointAddress;
            std::cout << "Found input endpoint" << std::endl;
        }
        else
        {
            _endpoint_out = ep->bEndpointAddress;
            std::cout << "Found output endpoint" << std::endl;
        }
    }

    std::cout << "Free config descriptor" << std::endl;
    libusb_free_config_descriptor(config);

    return true;
}

void Connector::release()
{
    if (_device)
    {
        libusb_release_interface(_device, 0);
        libusb_close(_device);
        _device = nullptr;
    }
}

void Connector::status(const char *status)
{
    std::cout << status << std::endl;
    if (_protocol)
        _protocol->onStatus(status);
}

void Connector::write_uint32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
}

int Connector::send(int cmd)
{
    if (!_connected)
        return 0;

    int transferred;
    uint8_t buffer[16];

    write_uint32_le(&buffer[0], 0x55AA55AA);
    write_uint32_le(&buffer[4], 0);
    write_uint32_le(&buffer[8], cmd);
    write_uint32_le(&buffer[12], ~cmd);

    std::unique_lock<std::mutex> lock(_write_mutex);
    libusb_bulk_transfer(_device, _endpoint_out, buffer, 16, &transferred, 0);

    return transferred;
}

int Connector::send(int cmd, uint8_t *data, uint32_t size)
{
    if (!_connected)
        return 0;

    int transferred;
    uint8_t buffer[16];

    write_uint32_le(&buffer[0], 0x55AA55AA);
    write_uint32_le(&buffer[4], size);
    write_uint32_le(&buffer[8], cmd);
    write_uint32_le(&buffer[12], ~cmd);

    std::unique_lock<std::mutex> lock(_write_mutex);
    libusb_bulk_transfer(_device, _endpoint_out, buffer, 16, &transferred, 0);
    libusb_bulk_transfer(_device, _endpoint_out, data, size, &transferred, 0);

    return transferred;
}

void Connector::read_loop()
{
    std::mutex mtx;
    std::condition_variable cv;
    Header header;
    int transferred = 0;
    uint8_t *data;

    // Set thread name
    setThreadName( "protocol-reader");
    while (_active)
    {
        if (!_connected)
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::seconds(1), [&]()
                        { return !_active.load(); });
            continue;
        }

        int result = libusb_bulk_transfer(_device, _endpoint_in, reinterpret_cast<uint8_t *>(&header), sizeof(Header), &transferred, READ_TIMEOUT);

        if (result == LIBUSB_ERROR_NO_DEVICE)
        {
            if (_protocol)
                _protocol->onDevice(false);
            _connected = false;
            continue;
        }

        if (result != LIBUSB_SUCCESS || transferred != sizeof(Header))
        {
            continue;
        }

        int padding = 0;
        if (header.type == 6)
            padding = _videoPadding;

        if (header.length > 0)
        {
            data = (uint8_t *)malloc(header.length + padding);
            if (!data)
                continue;
            libusb_bulk_transfer(_device, _endpoint_in, data, header.length, &transferred, READ_TIMEOUT);
        }

        if (!_protocol)
            free(data);
        else
        {
            if (padding > 0)
                std::fill(data + header.length, data + header.length + padding, 0);
            _protocol->onData(header.type, header.length, data);
        }
    }
}

void Connector::write_loop()
{
    std::mutex mtx;
    std::condition_variable cv;

    // Set thread name
    setThreadName("protocol-writer");

    while (_active)
    {
        _connected = connect(Settings::vendorid, Settings::productid);
        if (_connected)
        {
            status("Starting device");
            if (_protocol)
                _protocol->onDevice(true);

            status("Waiting for connecton");
            while (_connected && _active)
            {
                send(170);
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait_for(lock, std::chrono::seconds(2), [&]()
                            { return !_active.load(); });
            }
        }
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(1), [&]()
                    { return !_active.load(); });
    }
}