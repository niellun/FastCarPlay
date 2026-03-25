#include "connection.h"

#include <algorithm>
#include <ctime>
#include <sstream>
#include <stdexcept>

#include "libavcodec/defs.h"

#include "protocol/message.h"
#include "common/logger.h"
#include "protocol/protocol_const.h"
#include "common/functions.h"
#include "settings.h"

Connection::Connection()
    : writeQueue(WRITE_QUEUE_SIZE),
      videoStream(VIDEO_QUEUE_SIZE),
      audioStreamMain(AUDIO_QUEUE_SIZE),
      audioStreamAux(AUDIO_QUEUE_SIZE),
      _processQueue(Settings::usbBuffer, Settings::usbTransferSize),
      _transfers(Settings::usbQueue),
      _statusHandler(nullptr),
      _cipher(nullptr),
      _context(nullptr),
      _active(false),
      _connected(false),
      _phoneConnected(false),
      _ecnrypt(false),
      _state(PROTOCOL_STATUS_INITIALISING),
      _method("unknown"),
      _phoneName("phone"),
      _transfered(0)
{
    int result = libusb_init(&_context);
    if (result < 0)
        throw std::runtime_error(std::string("Can't initialise USB: ") + libusb_error_name(result));

    try
    {
        _cipher = new AESCipher(ENCRYPTION_BASE);
    }
    catch (const std::exception &e)
    {
        _cipher = nullptr;
        log_w("Can't initialise cypher for encryption > %s", e.what());
    }
    catch (...)
    {
        _cipher = nullptr;
        log_w("Can't initialise cypher for encryption > Unknown error");
    }

    for (Context &context : _transfers)
    {
        context.owner = this;
        context.transfer = nullptr;
        context.slot = nullptr;
    }

    log_v("Created");
}

Connection::~Connection()
{
    log_v("Destroying");
    stop();

    if (_cipher)
    {
        delete _cipher;
        _cipher = nullptr;
    }

    for (Context &context : _transfers)
    {
        if (context.transfer)
        {
            libusb_free_transfer(context.transfer);
            context.transfer = nullptr;
        }
    }

    if (_context)
    {
        libusb_exit(_context);
        _context = nullptr;
    }
    log_v("Destroyed");
}

void Connection::start()
{
    if (_active)
        return;

    _state = PROTOCOL_STATUS_INITIALISING;

    log_v("Starting");

    // Prepare usb transfers
    for (Context &context : _transfers)
    {
        if (!context.transfer)
        {
            context.transfer = libusb_alloc_transfer(0);
            if (context.transfer == nullptr)
            {
                log_e("Can't allocate usb transfer");
                return;
            }
        }
    }

    _active = true;
    _writeThread = std::thread(&Connection::mainLoop, this);
}

void Connection::stop()
{
    if (!_active)
        return;

    log_v("Stopping");

    _active = false;
    _connected = false;
    _state = PROTOCOL_STATUS_UNKNOWN;

    _processQueue.notify();
    writeQueue.notify();

    if (_writeThread.joinable())
        _writeThread.join();

    log_v("Stopped");
    _statusHandler = nullptr;
}

void Connection::onTransfer(libusb_transfer *transfer)
{
    if (!transfer || !transfer->user_data)
        return;

    Context *c = static_cast<Context *>(transfer->user_data);
    if (!c->owner->_connected)
        return;

    c->owner->_transfered.fetch_add(transfer->actual_length, std::memory_order_relaxed);
    log_p("Transfer %d [%d] > %s", transfer->actual_length, transfer->status, bytes(transfer->buffer, transfer->actual_length, 40).c_str());

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
        return;

    if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
    {
        c->owner->_connected = false;
        return;
    }

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        c->slot->commit(transfer->actual_length);
        c->slot = c->owner->_processQueue.get();
        if (!c->slot)
        {
            log_e("Can't allocate data slot for next usb transfer, increase usb buffer slots");
            c->owner->_connected = false;
            return;
        }
        c->transfer->buffer = c->slot->data;
    }
    int status = libusb_submit_transfer(c->transfer);
    if (status != LIBUSB_SUCCESS)
    {
        log_w("USB transfer re-submit failed with status %d", status);
        c->owner->_connected = false;
    }
}

void Connection::mainLoop()
{
    // Set thread name
    setThreadName("usb-write");

    log_d("USB writing thread started");

    int connectCount = 0;

    while (_active)
    {
        int linkCount = 0;
        libusb_device_handle *handler = libusb_open_device_with_vid_pid(_context, Settings::vendorid, Settings::productid);
        if (handler)
        {
            if (_state != PROTOCOL_STATUS_LINKING && _state != PROTOCOL_STATUS_ERROR)
                connectCount = 0;
            _state = PROTOCOL_STATUS_LINKING;
            linkCount = 1;
            uint8_t endpointIn = 0;
            uint8_t endpointOut = 0;
            libusb_device *device = nullptr;
            _ecnrypt = false;
            writeQueue.clear();

            while (!device && linkCount++ < LINK_RETRY)
            {
                device = link(handler, &endpointIn, &endpointOut);
                writeQueue.waitFor(_active, LINK_RETRY_TIMEOUT);
            }

            if (device)
            {
                _state = PROTOCOL_STATUS_ONLINE;
                onDeviceConnect(handler, device, endpointIn);
                writeLoop(handler, endpointOut);
                _state = PROTOCOL_STATUS_LINKING;
                onDeviceDisconnect();
            }

            libusb_release_interface(handler, 0);
            libusb_close(handler);
        }
        if (linkCount == 0)
        {
            if (_state != PROTOCOL_STATUS_NO_DEVICE && connectCount++ > CONNECT_RETRY)
                _state = PROTOCOL_STATUS_NO_DEVICE;
        }
        else
        {
            if (_state != PROTOCOL_STATUS_ERROR && connectCount++ > CONNECT_RETRY)
                _state = PROTOCOL_STATUS_ERROR;
        }
        writeQueue.waitFor(_active, RECONNECT_TIMEOUT);
    }

    log_v("USB writing thread stopped");
}

void Connection::onDeviceConnect(libusb_device_handle *handler, libusb_device *device, uint8_t endpointIn)
{
    _connected = true;
    _phoneConnected = false;
    log_i("Device connected %d:%d speed: %d", libusb_get_bus_number(device), libusb_get_device_address(device), libusb_get_device_speed(device));
    writeQueue.clear();
    videoStream.clear();
    audioStreamMain.clear();
    audioStreamAux.clear();
    _processQueue.reset();

    _processThread = std::thread(&Connection::processLoop, this);
    _readThread = std::thread(&Connection::readLoop, this);

    for (Context &context : _transfers)
    {
        context.owner = this;
        context.slot = _processQueue.get();
        if (context.slot == nullptr)
        {
            log_e("Can't allocate data slot for usb transfer, increase usb buffer slots");
            _connected = false;
            return;
        }
        libusb_fill_bulk_transfer(context.transfer, handler, endpointIn, context.slot->data, context.slot->size, Connection::onTransfer, &context, 0);
        int status = libusb_submit_transfer(context.transfer);
        if (status != LIBUSB_SUCCESS)
        {
            log_w("USB transfer submit failed with code %d", status);
            _connected = false;
            return;
        }
    }

    sendInit();
}

void Connection::onDeviceDisconnect()
{
    onPhoneDisconnect();

    log_i("Device disconnected");
    _connected = false;
    _state = PROTOCOL_STATUS_ERROR;
    _processQueue.notify();

    if (_readThread.joinable())
        _readThread.join();

    if (_processThread.joinable())
        _processThread.join();
}

void Connection::onPhoneConnect()
{
    if (_phoneConnected)
        return;
    _state = PROTOCOL_STATUS_CONNECTED;
    log_i("Phone connected");
    _phoneConnected = true;

    if (Settings::onConnect.value.length() > 1)
        execute(Settings::onConnect.value.c_str());
}

void Connection::onPhoneDisconnect()
{
    if (!_phoneConnected)
        return;
    _state = PROTOCOL_STATUS_ONLINE;
    log_i("Phone disconnected");
    _phoneConnected = false;

    _recorder.stop();
    if (Settings::onDisconnect.value.length() > 1)
        execute(Settings::onDisconnect.value.c_str());

    _method = "unknown";
    _phoneName = "phone";
}

void Connection::readLoop()
{
    setThreadName("usb-read");
    setThreadPriority(ThreadPriority::Realtime);
    timeval timeout{0, 1000};

    log_d("USB reading thread started");

    while (_connected)
    {
        libusb_handle_events_timeout_completed(_context, &timeout, nullptr);
    }

    log_v("Canceling transfer requests");

    for (Context &context : _transfers)
    {
        if (context.transfer)
            libusb_cancel_transfer(context.transfer);
        libusb_handle_events_timeout_completed(_context, &timeout, nullptr);
    }

    log_v("USB reading thread stopped");
}

void Connection::processLoop()
{
    setThreadName("usb-process");
    log_d("USB processing thread started");

    while (_connected)
    {
        std::unique_ptr<Message> message = std::make_unique<Message>();

        if (!_processQueue.read(message->header(), message->headerSize(), _connected))
            break;

        if (message->invalidMagic())
        {
            log_w("Header read failed > invalid magic");
            _processQueue.discard();
            continue;
        }

        if (message->invalidChecksum())
        {
            log_w("Header read failed > invalid checksum");
            _processQueue.discard();
            continue;
        }

        if (message->invalidLength())
        {
            log_w("Header read failed > invalid length");
            _processQueue.discard();
            continue;
        }

        if (message->length() > 0)
        {
            uint32_t padding = message->type() == CMD_VIDEO_DATA ? AV_INPUT_BUFFER_PADDING_SIZE : 0;
            uint8_t *buff = message->allocate(padding);
            if (!_processQueue.read(buff, message->length(), _connected))
                continue;
            if (!buff)
            {
                log_w("Message discarded > can't allocate memory %d", message->length() + padding);
                continue;
            }
        }

        onMessage(std::move(message));
    }

    log_v("USB processing thread stopped");
}

void Connection::writeLoop(libusb_device_handle *handler, uint8_t ep)
{
    while (_connected)
    {
        std::unique_ptr<Message> message = writeQueue.pop();
        if (!message)
        {
            if (!writeQueue.waitFor(_connected, PROTOCOL_HEARTBEAT_DELAY))
                break;
            message = writeQueue.pop();
        }

        if (!message)
            message = Message::HeartBeat();

        if (!_connected)
            break;

        if (!message->allocated())
            continue;

        while (message->isMotion() && writeQueue.peek() && writeQueue.peek()->isMotion())
        {
            message = writeQueue.pop();
        }

        if (_ecnrypt)
        {
            char error[256];
            if (!message->encrypt(_cipher, error))
            {
                log_w("Message encryption failed > %s", error);
                continue;
            }
        }

        int transferred;
        int status = libusb_bulk_transfer(handler, ep, message->header(), message->headerSize(), &transferred, PROTOCOL_HEARTBEAT_DELAY);
        message->setOffset(0);
        if (status == LIBUSB_SUCCESS && message->length() > 0)
        {
            libusb_bulk_transfer(handler, ep, message->data(), message->length(), &transferred, PROTOCOL_HEARTBEAT_DELAY);
        }
    }
}

libusb_device *Connection::link(libusb_device_handle *handler, uint8_t *epIn, uint8_t *epOut)
{
    if (fail(libusb_reset_device(handler), " Can't reset device"))
        return nullptr;

    if (fail(libusb_set_configuration(handler, 1), "Can't set configuration"))
        return nullptr;

    if (fail(libusb_claim_interface(handler, 0), "Can't claim interface"))
        return nullptr;

    libusb_device *device = libusb_get_device(handler);
    struct libusb_config_descriptor *config = nullptr;
    if (fail(libusb_get_active_config_descriptor(device, &config), "Can't get config"))
        return nullptr;

    for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++)
    {
        const struct libusb_endpoint_descriptor *ep = &config->interface[0].altsetting[0].endpoint[i];
        if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
            *epIn = ep->bEndpointAddress;
        else
            *epOut = ep->bEndpointAddress;
    }

    libusb_free_config_descriptor(config);
    return device;
}

void Connection::setEncryption(bool enabled)
{
    if (!enabled)
    {
        _ecnrypt = false;
        return;
    }

    if (!_cipher)
    {
        log_w("Can't enable encryption > Cipher is not initialised");
    }

    log_i("Encryption enabled");
    _ecnrypt = true;
}

bool Connection::fail(int status, const char *msg)
{
    if (status == 0)
        return false;
    log_w("%s > %s", msg, libusb_error_name(status));
    return true;
}

void Connection::sendInit()
{
    int syncTime = std::time(nullptr);
    int drivePosition = Settings::leftDrive ? 0 : 1; // 0==left, 1==right
    int nightMode = Settings::nightMode;             // 0==day, 1==night, 2==auto
    if (nightMode < 0 || nightMode > 2)
        nightMode = 2;
    int mic = 7;
    if (Settings::micType == 2)
        mic = 15;
    if (Settings::micType == 3)
        mic = 21;

    int width;
    int height;
    switch (Settings::androidMode)
    {
    default:
        width = 800;
        height = 480;
        break;
    case 2:
        width = 1280;
        height = 720;
        break;
    case 3:
        width = 1920;
        height = 1080;
        break;
    }

    if (Settings::width < Settings::height)
        std::swap(width, height);

    float scale = std::min((float)width / Settings::width, (float)height / Settings::height);
    width = Settings::width * scale;
    height = Settings::height * scale;

    log_i("Requesting carplay %dx%d@%d, android auto %dx%d", Settings::width.value, Settings::height.value, Settings::sourceFps.value, width, height);

    if (Settings::encryption)
    {
        if (_cipher)
            send(Message::Encryption(_cipher->seed()));
        else
            log_w("Can't request encryption > Cypher is not initalised");
    }

    if (Settings::dpi > 0)
        send(Message::File("/tmp/screen_dpi", Settings::dpi));
    send(Message::File("/etc/android_work_mode", 1));
    send(Message::Init(Settings::width, Settings::height, Settings::sourceFps));
    send(Message::String(
        CMD_JSON_CONTROL,
        "{\"syncTime\":%d,\"mediaDelay\":%d,\"drivePosition\":%d,"
        "\"androidAutoSizeW\":%d,\"androidAutoSizeH\":%d,\"HiCarConnectMode\":0,"
        "\"GNSSCapability\":7,\"DashboardInfo\":1,\"UseBTPhone\":0}",
        syncTime, Settings::mediaDelay.value, drivePosition, width, height));

    send(Message::String(CMD_DAYNIGHT, "{\"DayNightMode\":%d}", nightMode));

    send(Message::File("/tmp/night_mode", nightMode));
    send(Message::File("/tmp/charge_mode", Settings::weakCharge ? 0 : 2)); // Weak charge 0, other 2
    send(Message::File("/etc/box_name", "CarPlay"));
    send(Message::File("/tmp/hand_drive_mode", drivePosition));

    send(Message::Control(mic));
    send(Message::Control(Settings::wifi5 ? 25 : 24));
    send(Message::Control(Settings::bluetoothAudio ? 22 : 23));
    if (Settings::autoconnect)
        send(Message::Control(1002));
}

void Connection::onMessage(std::unique_ptr<Message> message)
{
    char error[256];
    if (!message->decrypt(_cipher, error))
    {
        log_w("Can't decrypt message %d > %s", message->type(), error);
        return;
    }

    if (message->type() == CMD_VIDEO_DATA && message->setOffset(20))
    {
        if (!videoStream.pushDiscard(std::move(message)))
            log_w("Discard message > video queue is full");
        return;
    }

    if (message->type() == CMD_AUDIO_DATA && message->length() > 16)
    {
        int channel = message->getInt(8);
        message->setOffset(12);
        if (channel == 1)
        {
            if (!audioStreamMain.pushDiscard(std::move(message)))
                log_w("Discard message > main audio queue is full");
            return;
        }
        if (channel == 2)
        {
            if (!audioStreamAux.pushDiscard(std::move(message)))
                log_w("Discard message > aux audio queue is full");
            return;
        }
    }

    if (message->type() == CMD_CONTROL && message->length() == 4)
    {
        switch (message->getInt(0))
        {
        case 1:
            _recorder.start(&writeQueue);
            return;

        case 2:
            _recorder.stop();
            return;
        }
    }

    if (message->type() == CMD_PLUGGED)
    {
        onPhoneConnect();
        return;
    }

    if (message->type() == CMD_UNPLUGGED)
    {
        onPhoneDisconnect();
        return;
    }

    if (message->type() == CMD_ENCRYPTION && message->length() == 0)
    {
        setEncryption(true);
        return;
    }

    if (message->type() == CMD_JSON_CONTROL)
    {
        char buf[64];
        log_d("Controll message %d [%d] > %s", message->type(), message->length(), ascii(message->data(), message->length()).c_str());
        if (jsonFindString(message->data(), message->length(), "MDLinkType", buf, 64))
            _method = buf;
        if (jsonFindString(message->data(), message->length(), "btName", buf, 64))
            _phoneName = buf;
        return;
    }

    log_v("Unknown message %d [%d] > %s", message->type(), message->length(), bytes(message->data(), message->length(), 40).c_str());
}

const std::string Connection::status() const
{
    std::ostringstream out;

    const libusb_version *version = libusb_get_version();
    out << "v"
        << static_cast<int>(version->major) << '.'
        << static_cast<int>(version->minor) << '.'
        << static_cast<int>(version->micro) << '.'
        << static_cast<int>(version->nano) << " "
        << " queue " << _processQueue.count() << " / " << Settings::usbBuffer << " "
        << (_ecnrypt.load(std::memory_order_acquire) ? "encrypt" : "simple") << " "
        << _phoneName << " via " << _method;

    return out.str();
}
