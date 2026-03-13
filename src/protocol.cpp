#include "protocol.h"
#include "helper/protocol_const.h"
#include "helper/functions.h"

#include <cstring>
#include <iomanip>
#include <cctype>

Protocol::Protocol(uint16_t width, uint16_t height, uint16_t fps, uint16_t padding)
    : Connector(padding),
      videoData(Settings::videoQueue),
      audioStreamMain(Settings::audioQueue),
      audioStreamAux(Settings::audioQueue),
      _recorder(Settings::audioQueue),
      _width(width),
      _height(height),
      _fps(fps),
      _phoneConnected(false)
{
}

Protocol::~Protocol()
{
    stop();
}

void Protocol::start(uint32_t evtStatus, uint32_t evtPhone)
{
    _evtStatusId = evtStatus;
    _evtPhoneId = evtPhone;
    Connector::start();
}

void Protocol::stop()
{
    Connector::stop();
}

void Protocol::sendConfig()
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

    if (_width < _height)
        std::swap(width, height);

    float scale = std::min((float)width / _width, (float)height / _height);
    width = _width * scale;
    height = _height * scale;

    std::cout << "[Protocol] Request android image " << width << "x" << height << std::endl;

    send(Command::String(
        CMD_JSON_CONTROL,
        "{\"syncTime\":%d,\"mediaDelay\":%d,\"drivePosition\":%d,"
        "\"androidAutoSizeW\":%d,\"androidAutoSizeH\":%d,\"HiCarConnectMode\":0,"
        "\"GNSSCapability\":7,\"DashboardInfo\":1,\"UseBTPhone\":0}",
        syncTime, Settings::mediaDelay.value, drivePosition, width, height));

    send(Command::String(CMD_DAYNIGHT, "{\"DayNightMode\":%d}", nightMode));

    send(Command::File("/tmp/night_mode", nightMode));
    send(Command::File("/tmp/charge_mode", Settings::weakCharge ? 0 : 2)); // Weak charge 0, other 2
    send(Command::File("/etc/box_name", "CarPlay"));
    send(Command::File("/tmp/hand_drive_mode", drivePosition));

    send(Command::Control(mic));
    send(Command::Control(Settings::wifi5 ? 25 : 24));
    send(Command::Control(Settings::bluetoothAudio ? 22 : 23));
    if (Settings::autoconnect)
        send(Command::Control(1002));
}

void Protocol::onStatus(uint8_t status)
{
    std::cout << "[Protocol] Status " << (int)status << std::endl;
    pushEvent(_evtStatusId, status);
}

void Protocol::onDevice(bool connected)
{
    if (connected)
    {
        if (Settings::encryption)
        {
            if (_cipher)
                send(Command::Encryption(_cipher->Seed()));
            else
                std::cout << "[Protocol] Can't enable encryption: cypher is not initalised" << std::endl;
        }
        if (Settings::dpi > 0)
            send(Command::File("/tmp/screen_dpi", Settings::dpi));
        send(Command::File("/etc/android_work_mode", 1));
        send(Command::Init(_width, _height, _fps));
        sendConfig();
    }
    else
    {
        onPhone(false);
        setEncryption(false);
    }
}

void Protocol::onPhone(bool connected)
{
    if (connected == _phoneConnected)
        return;
    _phoneConnected = connected;

    std::cout << (connected ? "[Protocol] Phone connected" : "[Protocol] Phone disconnected") << std::endl;

    if (!connected)
        _recorder.stop();

    pushEvent(_evtPhoneId, connected ? 1 : 0);

    if (connected && Settings::onConnect.value.length() > 1)
        execute(Settings::onConnect.value.c_str());

    if (!connected && Settings::onDisconnect.value.length() > 1)
        execute(Settings::onDisconnect.value.c_str());
}

void Protocol::onControl(int cmd)
{
    switch (cmd)
    {
    case 1:
        _recorder.start(this);
        break;

    case 2:
        _recorder.stop();
        break;
    }
}

void Protocol::onData(uint32_t cmd, uint32_t length, uint8_t *data)
{
    bool dispose = true;
    switch (cmd)
    {

    case CMD_CONTROL:
        if (length == 4)
        {
            int cmd = 0;
            memcpy(&cmd, data, sizeof(int));
            onControl(cmd);
        }
        break;

    case CMD_PLUGGED:
        onPhone(true);
        break;

    case CMD_UNPLUGGED:
        onPhone(false);
        break;

    case CMD_VIDEO_DATA:
    {
        if (length <= 20)
            break;
        videoData.pushDiscard(std::make_unique<Message>(data, length, 20));
        dispose = false;
        break;
    }
    case CMD_AUDIO_DATA:
    {
        if (length <= 16)
        {
            break;
        }
        int channel = 0;
        memcpy(&channel, data + 8, sizeof(int));
        if (channel == 1)
        {
            audioStreamMain.pushDiscard(std::make_unique<Message>(data, length, 12));
            dispose = false;
            break;
        }
        if (channel == 2)
        {
            audioStreamAux.pushDiscard(std::make_unique<Message>(data, length, 12));
            dispose = false;
            break;
        }
        break;
    }
    case CMD_ENCRYPTION:
        if (length == 0)
            setEncryption(true);
        break;
    }

    if (dispose && length > 0 && data)
        free(data);
}
