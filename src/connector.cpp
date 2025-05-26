#include "connector.h"

#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <condition_variable>

#include "helper/protocol_const.h"
#include "helper/functions.h"
#include "settings.h"

Connector::Connector(uint16_t videoPadding)
    : _videoPadding(videoPadding)
{
    try
    {
        _cipher = new AESCipher(ENCRYPTION_BASE);
    }
    catch (...)
    {
        _cipher = nullptr;
    }

    int result = libusb_init(&_context);
    if (result < 0)
        throw std::runtime_error(std::string("Can't initialise USB: ") + libusb_error_name(result));
}

Connector::~Connector()
{
    stop();

    if (_cipher)
    {
        delete _cipher;
        _cipher = nullptr;
    }

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
    status("Searching for dongle");

    _device = libusb_open_device_with_vid_pid(_context, vendor_id, product_id);
    if (!_device)
    {
        std::cout << "[Connection] Failed to create device handle - no device" << std::endl;
        status("Can't find dongle");
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
    int usbres = 0;
    status("Linking dongle");

    usbres = libusb_reset_device(_device);
    if (usbres < 0)
    {
        std::cout << "[Connection] Can't reset device: " << libusb_error_name(usbres) << std::endl;
        return false;
    }

    usbres = libusb_set_configuration(_device, 1);
    if (usbres < 0)
    {
        std::cout << "[Connection] Can't set configuration: " << libusb_error_name(usbres) << std::endl;
        return false;
    }

    usbres = libusb_claim_interface(_device, 0);
    if (usbres < 0)
    {
        std::cout << "[Connection] Can't claim interface: " << libusb_error_name(usbres) << std::endl;
        return false;
    }

    libusb_device *dev = libusb_get_device(_device);
    struct libusb_config_descriptor *config = nullptr;
    usbres = libusb_get_active_config_descriptor(dev, &config);
    if (usbres < 0)
    {
        std::cout << "[Connection] Can't get config: " << libusb_error_name(usbres) << std::endl;
        return false;
    }

    for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++)
    {
        const struct libusb_endpoint_descriptor *ep = &config->interface[0].altsetting[0].endpoint[i];
        if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
        {
            _endpoint_in = ep->bEndpointAddress;
        }
        else
        {
            _endpoint_out = ep->bEndpointAddress;
        }
    }

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
    if (_protocol)
        _protocol->onStatus(status);
}

int Connector::send(int cmd, bool encrypt, uint8_t *data, uint32_t size)
{
    if (!_connected)
        return 0;

    int transferred;
    uint8_t buffer[16];
    uint32_t magic = MAGIC;
    encrypt = encrypt && _ecnrypt;

    if (encrypt && data && size > 0)
    {
        if (_cipher->Encrypt(data, size))
            magic = MAGIC_ENC;
    }

    write_uint32_le(&buffer[0], magic);
    write_uint32_le(&buffer[4], size);
    write_uint32_le(&buffer[8], cmd);
    write_uint32_le(&buffer[12], ~cmd);

    std::unique_lock<std::mutex> lock(_write_mutex);
    libusb_bulk_transfer(_device, _endpoint_out, buffer, 16, &transferred, 0);
    if (data && size > 0)
        libusb_bulk_transfer(_device, _endpoint_out, data, size, &transferred, 0);

#ifdef PROTOCOL_DEBUG
    printMessage(cmd, size, data, magic == MAGIC_ENC, true);
#endif

    return transferred;
}

void Connector::setEncryption(bool enabled)
{
    if (!enabled)
    {
        _ecnrypt = false;
        return;
    }
    if (!_cipher)
    {
        std::cout << "[Connection] Can't enable encryption: cypher initialisation failed" << std::endl;
        return;
    }

    std::cout << "[Connection] Encryption enabled" << std::endl;
    _ecnrypt = true;
}

void Connector::printInts(uint32_t length, uint8_t *data, uint16_t max)
{
    if (data && length >= 4)
    {
        std::cout << "  > ";
        size_t count = length / 4;
        for (size_t i = 0; (i < count) & (i < max); ++i)
        {
            uint32_t val =
                ((uint32_t)data[i * 4 + 0]) |
                ((uint32_t)data[i * 4 + 1] << 8) |
                ((uint32_t)data[i * 4 + 2] << 16) |
                ((uint32_t)data[i * 4 + 3] << 24);
            std::cout << val;
        }
        std::cout << std::endl;
    }
}

void Connector::printBytes(uint32_t length, uint8_t *data, uint16_t max)
{
    if (data && length >= 4)
    {
        std::cout << "  > ";
        for (size_t i = 0; (i < length) & (i < max); ++i)
        {
            std::cout << std::setw(4) << (uint32_t)(data[i]);
        }
        std::cout << std::endl;
    }
}

const char *Connector::cmdString(int cmd)
{
    for (const ProtocolCmdEntry &entry : protocolCmdList)
    {
        if (entry.cmd == cmd)
            return entry.name;
    }
    return nullptr;
}

void Connector::printMessage(uint32_t cmd, uint32_t length, uint8_t *data, bool encrypted, bool out)
{
    if (Settings::protocolDebug <= PROTOCOL_DEBUG_NONE)
        return;

    const char *cmds = cmdString(cmd);

    if (Settings::protocolDebug <= PROTOCOL_DEBUG_UNKNOWN && cmds)
        return;

    if (Settings::protocolDebug < PROTOCOL_DEBUG_OUT && out)
        return;

    bool stream = (cmd == CMD_AUDIO_DATA || cmd == CMD_VIDEO_DATA) && length > 50;
    if (Settings::protocolDebug < PROTOCOL_DEBUG_ALL && stream)
        return;

    std::ostringstream oss;

    oss << (out ? "<" : ">") << (encrypted ? "*" : " ")
        << std::setw(3) << std::right << static_cast<unsigned>(cmd)
        << std::setw(8) << std::left << ("[" + std::to_string(length) + "]")
        << std::setw(15) << std::left << (cmds ? cmds : "Unknown");

    if (data && length > 0)
    {
        for (size_t i = 0; i < 50 && i < length; ++i)
        {
            char ch = static_cast<char>(data[i]);
            if (ch == '\n' || ch == '\r')
                oss << '.';
            else
                oss << (std::isprint(static_cast<unsigned char>(ch)) ? ch : '.');
        }
    }

    std::cout << oss.str() << std::endl;
}

void Connector::read_loop()
{
    std::mutex mtx;
    std::condition_variable cv;
    Header header;
    int transferred = 0;
    uint8_t *data = nullptr;

    // Set thread name
    setThreadName("protocol-reader");
    while (_active && _connected)
    {
        int result = libusb_bulk_transfer(_device, _endpoint_in, reinterpret_cast<uint8_t *>(&header), sizeof(Header), &transferred, READ_TIMEOUT);

        if (result == LIBUSB_ERROR_NO_DEVICE)
        {
            std::cout << "[Connection] Device disconnected" << std::endl;
            if (_protocol)
                _protocol->onDevice(false);
            _connected = false;
            continue;
        }

        if (result != LIBUSB_SUCCESS || transferred != sizeof(Header))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        {
            free(data);
            continue;
        }

        if (header.magic == MAGIC_ENC)
        {
            if (!_cipher)
            {
                std::cout << "[Connection] Received encrypted command " << header.type << " but cipher is not initialised" << std::endl;
                continue;
            }
            if (!_cipher->Decrypt(data, header.length))
            {
                std::cout << "[Connection] Can't decrypt command " << header.type << std::endl;
                continue;
            }
        }

#ifdef PROTOCOL_DEBUG
        printMessage(header.type, header.length, data, header.magic == MAGIC_ENC, false);

        if (header.type == 7 && header.length < 100)
            printBytes(header.length, data, 30);
#endif

        if (padding > 0)
            std::fill(data + header.length, data + header.length + padding, 0);
        _protocol->onData(header.type, header.length, data);
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
            status("Initialising dongle");
            std::cout << "[Connection] Device connected" << std::endl;

            _read_thread = std::thread(&Connector::read_loop, this);
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