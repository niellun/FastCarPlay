#include "connector.h"

#include <stdexcept>
#include <iostream>
#include <iomanip>

#include "protocol/message.h"
#include "common/logger.h"
#include "protocol/protocol_const.h"
#include "common/functions.h"
#include "settings.h"

Connector::Connector()
    : writeQueue(WRITE_QUEUE_SIZE),
      videoStream(Settings::videoQueue),
      audioStreamMain(Settings::audioQueue),
      audioStreamAux(Settings::audioQueue),
      _recorder(Settings::audioQueue),
      _cipher(nullptr),
      _context(nullptr),
      _active(false),
      _connected(false),
      _ecnrypt(false),
      _state(PROTOCOL_STATUS_INITIALISING),
      _failCount(0),
      _nodeviceCount(0),
      _statusHandler(nullptr)
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
}

Connector::~Connector()
{
    stop();

    if (_cipher)
    {
        delete _cipher;
        _cipher = nullptr;
    }

    if (_context)
    {
        libusb_exit(_context);
        _context = nullptr;
    }
}

void Connector::start(atomic<int8_t>* statusHandler)
{
    _statusHandler = statusHandler;

    if (_active)
        return;

    _active = true;
    _thread = std::thread(&Connector::mainLoop, this);
}

void Connector::stop()
{
    if (!_active)
        return;

    _active = false;
    writeQueue.notify();
    state(PROTOCOL_STATUS_INITIALISING);

    if (_thread.joinable())
        _thread.join();

    _statusHandler = nullptr;        
}

void Connector::mainLoop()
{
    // Set thread name
    setThreadName("usb-write");
    state(PROTOCOL_STATUS_LINKING);

    while (_active)
    {
        libusb_device_handle *handler = libusb_open_device_with_vid_pid(_context, Settings::vendorid, Settings::productid);
        if (handler)
        {
            uint8_t epIn = 0;
            uint8_t epOut = 0;
            int retry = 0;
            libusb_device *device = nullptr;
            _ecnrypt = false;
            writeQueue.clear();

            while (!device && retry++ < LINK_RETRY)
            {
                device = link(handler, &epIn, &epOut);
                writeQueue.waitFor(_active, LINK_RETRY_TIMEOUT);
            }

            if (device)
            {
                _connected = true;
                state(PROTOCOL_STATUS_ONLINE);
                log_i("Device connected %d:%d speed: %d", libusb_get_bus_number(device), libusb_get_device_address(device), libusb_get_device_speed(device));
                _reader.start(_context, handler, epIn, this);
                onConnect();

                writeLoop(handler, epOut);

                _connected = false;
                onDisconnect();
                _reader.stop();
            }

            libusb_release_interface(handler, 0);
            libusb_close(handler);
        }
        state(PROTOCOL_STATUS_NO_DEVICE);
        writeQueue.waitFor(_active, RECONNECT_TIMEOUT);
    }
}

void Connector::writeLoop(libusb_device_handle *handler, uint8_t ep)
{
    while (_active && _reader.active())
    {
        std::unique_ptr<Message> message = writeQueue.pop();
        if (!message)
        {
            if (!writeQueue.waitFor(_active, PROTOCOL_HEARTBEAT_DELAY))
                break;
            message = writeQueue.pop();
        }

        if (!message)
            message = Message::HeartBeat();

        if (!_active || !_reader.active())
            break;

        if (!message->allocated())
            continue;

        while (message->isMotion() && writeQueue.peek() && writeQueue.peek()->isMotion())
        {
            message = writeQueue.pop();
        }

        if (_ecnrypt)
        {
            Status s = message->encrypt(_cipher);
            if (s.failed())
            {
                log_w("Message encryption failed > %s", s.error());
                continue;
            }
        }

        int transferred;
        libusb_bulk_transfer(handler, ep, message->header(), message->headerSize(), &transferred, 0);
        if (message->length() > 0)
            libusb_bulk_transfer(handler, ep, message->data(), message->length(), &transferred, 0);
    }
}

libusb_device *Connector::link(libusb_device_handle *handler, uint8_t *epIn, uint8_t *epOut)
{
    state(PROTOCOL_STATUS_LINKING);

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

void Connector::setEncryption(bool enabled)
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

bool Connector::fail(int status, const char *msg)
{
    if (status == 0)
        return false;
    log_w("%s > %s", msg, libusb_error_name(status));
    state(PROTOCOL_STATUS_ERROR);
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
        if (_statusHandler)
            *_statusHandler = state;
        return true;
    }

    if (state == PROTOCOL_STATUS_NO_DEVICE && (_nodeviceCount++ > 10 || _state >= PROTOCOL_STATUS_ONLINE))
    {
        _failCount = 0;
        _state = state;
        if (_statusHandler)
            *_statusHandler = state;
        return true;
    }

    if (state == PROTOCOL_STATUS_ONLINE && _state == PROTOCOL_STATUS_CONNECTED)
    {
        _state = state;
        if (_statusHandler)
            *_statusHandler = state;
        return true;
    }

    return false;
}

void Connector::onConnect()
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

    log_i("Requesting carplay %dx%d@%d, android auto %dx%d", Settings::width.value, Settings::height.value, Settings::fps.value, width, height);

    if (Settings::encryption)
    {
        if (_cipher)
            send(Message::Encryption(_cipher->Seed()));
        else
            log_w("Can't request encryption > Cypher is not initalised");
    }

    if (Settings::dpi > 0)
        send(Message::File("/tmp/screen_dpi", Settings::dpi));
    send(Message::File("/etc/android_work_mode", 1));
    send(Message::Init(Settings::width, Settings::height, Settings::fps));
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

    if (Settings::onConnect.value.length() > 1)
        execute(Settings::onConnect.value.c_str());
}

void Connector::onDisconnect()
{
    _recorder.stop();
    if (Settings::onDisconnect.value.length() > 1)
        execute(Settings::onDisconnect.value.c_str());
}

void Connector::onMessage(std::unique_ptr<Message> message)
{
    Status s = message->decrypt(_cipher);
    if (s.failed())
    {
        log_w("Can't decrypt message %d > %s", message->type(), s.error());
        return;
    }

    switch (message->type())
    {

    case CMD_CONTROL:
        if (message->length() == 4)
        {
            switch (message->getInt(0))
            {
            case 1:
                _recorder.start(&writeQueue);
                break;

            case 2:
                _recorder.stop();
                break;
            }
        }
        break;

    case CMD_PLUGGED:
        state(PROTOCOL_STATUS_CONNECTED);
        break;

    case CMD_UNPLUGGED:
        state(PROTOCOL_STATUS_ONLINE);
        break;

    case CMD_VIDEO_DATA:
    {
        if (message->setOffset(20))
            videoStream.pushDiscard(std::move(message));
        break;
    }
    case CMD_AUDIO_DATA:
    {
        if (message->length() <= 16)
            break;
        int channel = message->getInt(8);
        message->setOffset(12);
        if (channel == 1)
            audioStreamMain.pushDiscard(std::move(message));
        if (channel == 2)
            audioStreamAux.pushDiscard(std::move(message));
        break;
    }
    case CMD_ENCRYPTION:
        if (message->length() == 0)
            setEncryption(true);
        break;
    }
}
