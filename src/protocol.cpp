#include "protocol.h"
#include "helper/protocol_const.h"
#include "helper/functions.h"

#include <cstring>
#include <iomanip>
#include <cctype>

Protocol::Protocol(uint16_t width, uint16_t height, uint16_t fps, uint16_t padding)
    : connector(padding),
      videoData(Settings::videoQueue),
      audioStreamMain(Settings::audioQueue),
      audioStreamAux(Settings::audioQueue),
      phoneConnected(false),
      _width(width),
      _height(height),
      _fps(fps)
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
    connector.start(this);
}

void Protocol::stop()
{
    connector.stop();
}

void Protocol::sendInit(int width, int height, int fps)
{
    uint8_t buf[28];
    write_uint32_le(&buf[0], width);
    write_uint32_le(&buf[4], height);
    write_uint32_le(&buf[8], fps);
    write_uint32_le(&buf[12], 5);
    write_uint32_le(&buf[16], 49152);
    write_uint32_le(&buf[20], 2);
    write_uint32_le(&buf[24], 2);

    connector.send(1, true, buf, 28);
}

void Protocol::sendConfig()
{
    int syncTime = std::time(nullptr);
    int mediaDelay = 300;
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

    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "{\"syncTime\":%d,\"mediaDelay\":%d,\"drivePosition\":%d,"
             "\"androidAutoSizeW\":%d,\"androidAutoSizeH\":%d,\"HiCarConnectMode\":0,"
             "\"GNSSCapability\":7,\"DashboardInfo\":1,\"UseBTPhone\":0}",
             syncTime, mediaDelay, drivePosition, width, height);

    sendString(CMD_JSON_CONTROL, buffer);

    snprintf(buffer, sizeof(buffer), "{\"DayNightMode\":%d}", nightMode);
    sendString(CMD_DAYNIGHT, buffer);

    sendFile("/tmp/night_mode", nightMode);
    sendFile("/tmp/charge_mode", Settings::weakCharge ? 0 : 2); // Weak charge 0, other 2
    sendFile("/etc/box_name", "CarPlay");
    sendFile("/tmp/hand_drive_mode", drivePosition);

    sendInt(CMD_CONTROL, mic);
    sendInt(CMD_CONTROL, Settings::wifi5 ? 25 : 24);
    sendInt(CMD_CONTROL, Settings::bluetoothAudio ? 22 : 23);
    if (Settings::autoconnect)
        sendInt(CMD_CONTROL, 1002);
}

void Protocol::sendKey(int key)
{
    uint8_t buf[4];
    write_uint32_le(&buf[0], key);

    connector.send(8, false, buf, 4);
}

void Protocol::sendFile(const char *filename, const char *value)
{
    uint32_t len = strlen(value);
    if (len > 16)
    {
        throw std::invalid_argument("String too long (max 16 bytes)");
    }

    // note: we send only the ASCII bytes, no trailing '\0'
    sendFile(filename,
             reinterpret_cast<const uint8_t *>(value),
             static_cast<uint32_t>(len));
}

// overload for a single 32‑bit integer
void Protocol::sendFile(const char *filename, int value)
{
    uint8_t buf[4];
    write_uint32_le(buf, value);
    sendFile(filename, buf, 4);
}

void Protocol::sendClick(float x, float y, bool down)
{
    uint8_t buf[16];
    write_uint32_le(buf, down ? 14 : 16);
    write_uint32_le(buf + 4, int(10000 * x));
    write_uint32_le(buf + 8, int(10000 * y));
    write_uint32_le(buf + 12, 0);
    connector.send(5, false, buf, 16);
}

void Protocol::sendMove(float dx, float dy)
{
    uint8_t buf[16];
    write_uint32_le(buf, 15);
    write_uint32_le(buf + 4, int(10000 * dx));
    write_uint32_le(buf + 8, int(10000 * dy));
    write_uint32_le(buf + 12, 0);
    connector.send(5, false, buf, 16);
}

void Protocol::sendFile(const char *filename, const uint8_t *data, uint32_t length)
{
    // filename is assumed null‑terminated, so strlen + 1 to include the '\0'
    uint32_t fn_len = strlen(filename) + 1;

    // Total buffer size: 4 (fn_len) + fn_len + 4 (content_len) + content_len
    uint32_t total = 4 + fn_len + 4 + length;
    std::vector<uint8_t> result(total);
    uint8_t *buf = result.data();

    // 1) filename length (LE)
    write_uint32_le(buf, fn_len);
    buf += 4;

    // 2) filename bytes (including the '\0')
    std::memcpy(buf, filename, fn_len);
    buf += fn_len;

    // 3) content length (LE)
    write_uint32_le(buf, length);
    buf += 4;

    // 4) content bytes
    std::memcpy(buf, data, length);

    connector.send(CMD_SEND_FILE, true, result.data(), total);
}

void Protocol::sendInt(uint32_t cmd, uint32_t value, bool encryption)
{
    uint8_t buf[4];
    write_uint32_le(buf, value);
    connector.send(cmd, encryption, buf, 4);
}

void Protocol::sendString(uint32_t cmd, char *str, bool encryption)
{
    uint32_t total = strlen(str);
    connector.send(cmd, true, (uint8_t *)str, total);
}

void Protocol::sendEncryption()
{
    AESCipher *cypher = connector.Cypher();
    if (!cypher)
    {
        std::cout << "[Protocol] Can't enable encryption: cypher is not initalised";
        return;
    }
    uint8_t buf[4];
    write_uint32_le(buf, cypher->Seed());
    connector.send(CMD_ENCRYPTION, false, buf, 4);
}

void Protocol::onStatus(uint8_t status)
{
    pushEvent(_evtStatusId, status);
}

void Protocol::onDevice(bool connected)
{
    if (connected)
    {
        if (Settings::encryption)
            sendEncryption();
        if (Settings::dpi > 0)
            sendFile("/tmp/screen_dpi", Settings::dpi);
        sendFile("/etc/android_work_mode", 1);
        sendInit(_width, _height, _fps);
        sendConfig();
    }
    else
    {
        onPhone(false);
        connector.setEncryption(false);
    }
}

void Protocol::onPhone(bool connected)
{
    if (connected == phoneConnected)
        return;
    phoneConnected = connected;

    std::cout << (connected ? "[Protocol] Phone connected" : "[Protocol] Phone disconnected") << std::endl;

    pushEvent(_evtPhoneId, connected ? 1 : 0);

    if (connected && Settings::onConnect.value.length() > 1)
        execute(Settings::onConnect.value.c_str());

    if (!connected && Settings::onDisconnect.value.length() > 1)
        execute(Settings::onDisconnect.value.c_str());
}

void Protocol::onData(uint32_t cmd, uint32_t length, uint8_t *data)
{
    bool dispose = true;
    switch (cmd)
    {
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
            connector.setEncryption(true);
        break;
    }

    if (dispose && length > 0 && data)
        free(data);
}
