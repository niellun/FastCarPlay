#include "connector.h"

#include <stdexcept>
#include <iostream>
#include <iomanip>

#include "helper/protocol_const.h"
#include "helper/functions.h"
#include "settings.h"

Connector::Connector()
{
    try
    {
        _cipher = new AESCipher(ENCRYPTION_BASE);
    }
    catch (...)
    {
        _cipher = nullptr;
    }

    _state = PROTOCOL_STATUS_INITIALISING;
    int result = libusb_init(&_context);
    if (result < 0)
        throw std::runtime_error(std::string("Can't initialise USB: ") + libusb_error_name(result));

    _usbTransfers = Settings::usbQueue;
    if(_usbTransfers > MAX_USB_REQUESTS)
        _usbTransfers = MAX_USB_REQUESTS;

    for (int i = 0; i < _usbTransfers; i++)
    {
        _usbTransfer[i] = libusb_alloc_transfer(0);
        uint8_t *buf = static_cast<uint8_t *>(malloc(Settings::usbBufferSize));
        libusb_fill_bulk_transfer(_usbTransfer[i], _device, _endpoint_in, buf, Settings::usbBufferSize, Connector::onUsbRead, this, READ_TIMEOUT);
    }
}

Connector::~Connector()
{
    _active = false;
    _queue.notify();
    libusb_interrupt_event_handler(_context);

    if (_write_thread.joinable())
        _write_thread.join();

    if (_read_thread.joinable())
        _read_thread.join();

    if (_cipher)
    {
        delete _cipher;
        _cipher = nullptr;
    }

    for (int i = 0; i < _usbTransfers; i++)
    {
        if (_usbTransfer[i])
        {
            timeval timeout{0, 100000};            
            libusb_cancel_transfer(_usbTransfer[i]);
            libusb_handle_events_timeout_completed(_context, &timeout, nullptr);            
        }
    }

    for (int i = 0; i < _usbTransfers; i++)
    {
        if (_usbTransfer[i])
        {
            if (_usbTransfer[i]->buffer)
                free(_usbTransfer[i]->buffer);
            libusb_free_transfer(_usbTransfer[i]);
            _usbTransfer[i] = nullptr;
        }
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

void Connector::start()
{
    if (_active)
        stop();

    _active = true;
    _write_thread = std::thread(&Connector::writeLoop, this);
}

void Connector::stop()
{
    if (!_active)
        return;

    _active = false;
    _queue.notify();
    libusb_interrupt_event_handler(_context);

    state(PROTOCOL_STATUS_INITIALISING);

    if (_write_thread.joinable())
        _write_thread.join();

    if (_read_thread.joinable())
        _read_thread.join();        
}

bool Connector::connect(uint16_t vendor_id, uint16_t product_id)
{
    _device = libusb_open_device_with_vid_pid(_context, vendor_id, product_id);
    if (!_device)
    {
        std::cout << "[Connection] Failed to create device handle - no device" << std::endl;
        state(PROTOCOL_STATUS_NO_DEVICE);
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
    state(PROTOCOL_STATUS_LINKING);

    if (linkFail(libusb_reset_device(_device), " Can't reset device"))
        return false;

    if (linkFail(libusb_set_configuration(_device, 1), "Can't set configuration"))
        return false;

    if (linkFail(libusb_claim_interface(_device, 0), "Can't claim interface"))
        return false;

    libusb_device *dev = libusb_get_device(_device);
    struct libusb_config_descriptor *config = nullptr;
    if (linkFail(libusb_get_active_config_descriptor(dev, &config), "Can't get config"))
        return false;

    for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++)
    {
        const struct libusb_endpoint_descriptor *ep = &config->interface[0].altsetting[0].endpoint[i];
        if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
            _endpoint_in = ep->bEndpointAddress;
        else
            _endpoint_out = ep->bEndpointAddress;
    }

    libusb_free_config_descriptor(config);
    return true;
}

bool Connector::state(u_int8_t state)
{
    if (state == _state)
        return false;

    if (state == PROTOCOL_STATUS_ERROR && _failCount++ < 10)
    {
        return false;
    }

    if (state > _state || state == PROTOCOL_STATUS_INITIALISING)
    {
        _nodeviceCount = 0;
        _failCount = 0;
        _state = state;
        onStatus(state);
        return true;
    }

    if (state == PROTOCOL_STATUS_NO_DEVICE && (_nodeviceCount++ > 10 || _state >= PROTOCOL_STATUS_ONLINE))
    {
        _failCount = 0;
        _state = state;
        onStatus(state);
        return true;
    }

    return false;
}

bool Connector::linkFail(int status, const char *msg)
{
    if (status == 0)
        return false;
    std::cout << "[Connection] " << msg << ": " << libusb_error_name(status) << std::endl;
    state(PROTOCOL_STATUS_ERROR);
    return true;
}

void Connector::onDisconnect()
{
    if (!_connected)
        return;
    std::cout << "[Connection] Device disconnected" << std::endl;
    _connected = false;
}

bool Connector::send(std::unique_ptr<Command> packet)
{
    if (!_connected || !packet)
        return false;

    return _queue.pushDiscard(std::move(packet));
}

int Connector::write(int cmd, bool encrypt, uint8_t *data, uint32_t size)
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

void Connector::printInts(uint8_t *data, uint32_t length, uint16_t max)
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
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
}

void Connector::printBytes(uint8_t *data, uint32_t length, uint16_t max)
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

    bool stream = (cmd == CMD_AUDIO_DATA || cmd == CMD_VIDEO_DATA) && length > 150;
    stream = stream || cmd == CMD_HEARTBEAT;
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

void Connector::onUsbRead(libusb_transfer *transfer)
{
    Connector *c = static_cast<Connector *>(transfer->user_data);

    if (!c->_active)
        return;

    if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
    {
        c->onDisconnect();
        return;
    }

    if (c->_active && transfer->status == LIBUSB_TRANSFER_COMPLETED && transfer->actual_length > 0)
        c->onData(transfer->buffer, transfer->actual_length);

    if (c->_active && (libusb_submit_transfer(transfer) != LIBUSB_SUCCESS))
    {
        std::cout << "[Connection] USB transfer re-submit failed" << std::endl;
        c->onDisconnect();
    }
}

void Connector::readLoop()
{
    setThreadName("protocol-reader");
    timeval timeout{0, 100000};

    for (int i = 0; i < _usbTransfers; i++)
    {
        _usbTransfer[i]->dev_handle = _device;
        _usbTransfer[i]->endpoint = _endpoint_in;
        int status = libusb_submit_transfer(_usbTransfer[i]);
        if (status != LIBUSB_SUCCESS)
        {
            std::cout << "[Connection] USB transfer submit " << i << " failed: " << status << std::endl;
            onDisconnect();
            return;
        }
    }

    while (_active && _connected)
    {
        libusb_handle_events_timeout_completed(_context, &timeout, nullptr);
    }
}

void Connector::writeLoop()
{
    // Set thread name
    setThreadName("protocol-writer");
    state(PROTOCOL_STATUS_LINKING);

    while (_active)
    {
        _connected = connect(Settings::vendorid, Settings::productid);
        if (_connected)
        {
            std::cout << "[Connection] Device connected" << std::endl;

            _read_thread = std::thread(&Connector::readLoop, this);
            onDevice(true);

            state(PROTOCOL_STATUS_ONLINE);
            while (_connected && _active)
            {
                std::unique_ptr<Command> message = _queue.pop();
                if (!message)
                {
                    if (!_queue.waitFor(_active, 2000))
                        break;
                    message = _queue.pop();
                }

                if (message)
                    write(message->command, message->encryption, message->data, message->length);
                else
                    write(CMD_HEARTBEAT, false, nullptr, 0);
            }

            if (_active)
            {
                state(PROTOCOL_STATUS_NO_DEVICE);
                onDevice(false);
            }

            _queue.clear();

            if (_read_thread.joinable())
                _read_thread.join();
        }
        _queue.waitFor(_active, 1000);
    }
}
