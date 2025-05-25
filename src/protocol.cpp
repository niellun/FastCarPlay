#include "protocol.h"
#include "helper/protocol_const.h"
#include "helper/functions.h"

#include <cstring>
#include <iomanip>
#include <cctype>

Protocol::Protocol(uint16_t width, uint16_t height, uint16_t fps, uint16_t padding)
    : connector(padding),
      videoData(Settings::queue),
      audioStream0(Settings::queue),
      audioStream1(Settings::queue),
      audioStream2(Settings::queue),
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

const char *Protocol::cmdString(int cmd)
{
    for (const ProtocolCmdEntry &entry : protocolCmdList)
    {
        if (entry.cmd == cmd)
            return entry.name;
    }
    return "Unknown";
}

void Protocol::start(StatusCallback onStatus)
{
    _statusCallback = onStatus;
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

void Protocol::sendKey(int key)
{
    printf("Send key %d", key);

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

void Protocol::onStatus(const char *status)
{
    if (_statusCallback)
        _statusCallback(status);
}

void Protocol::onDevice(bool connected)
{
    if (connected)
    {
        if (Settings::encryption)
            sendEncryption();
        sendInit(_width, _height, _fps);
        if (Settings::dpi > 0)
            sendFile("/tmp/screen_dpi", Settings::dpi);
        sendFile("/etc/android_work_mode", 1);
        sendFile("/tmp/night_mode", 2);      // 0==day, 1==night, 2==???
        sendFile("/tmp/hand_drive_mode", 0); // 0==left, 1==right
        sendFile("/tmp/charge_mode", 0);
        sendFile("/etc/box_name", "CarPlay");
        if (Settings::autoconnect)
            sendInt(CMD_CONTROL, 1002);
        if (Settings::encryption)
            sendEncryption();
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
        if (length <= 13)
        {
            print_message(cmd, length, data);
            break;
        }
        int channel = 0;
        memcpy(&channel, data + 8, sizeof(int));
        if (channel == 0)
        {
            audioStream0.pushDiscard(std::make_unique<Message>(data, length, 12));
            dispose = false;
            break;
        }
        if (channel == 1)
        {
            audioStream1.pushDiscard(std::make_unique<Message>(data, length, 12));
            dispose = false;
            break;
        }
        if (channel == 2)
        {
            audioStream2.pushDiscard(std::make_unique<Message>(data, length, 12));
            dispose = false;
            break;
        }
        print_message(cmd, length, data);
        break;
    }
    case CMD_PLUGGED:
    {
        onPhone(true);
        break;
    }
    case CMD_UNPLUGGED:
    {
        onPhone(false);
        break;
    }
    case CMD_ENCRYPTION:
    {
        if (length == 0)
            connector.setEncryption(true);
        print_message(cmd, length, data);
        break;
    }

    default:
        print_message(cmd, length, data);
        break;
    }

    if (dispose && length > 0 && data)
        free(data);
}

void Protocol::print_ints(uint32_t length, uint8_t *data, uint16_t max)
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
        std::cout << endl;
    }
}

void Protocol::print_bytes(uint32_t length, uint8_t *data, uint16_t max)
{
    if (data && length >= 4)
    {
        std::cout << "  > ";
        for (size_t i = 0; (i < length) & (i < max); ++i)
        {
            std::cout << data[i];
        }
        std::cout << endl;
    }
}

void Protocol::print_message(uint32_t cmd, uint32_t length, uint8_t *data)
{
    std::cout << "> "
              << std::setw(3) << std::right << static_cast<unsigned>(cmd)
              << std::setw(8) << std::left << ("[" + std::to_string(length) + "]")
              << std::setw(15) << std::left << cmdString(cmd);

    if (data && length > 0)
    {
        for (size_t i = 0; i < 50 && i < length; ++i)
        {
            char ch = static_cast<char>(data[i]);
            std::cout << (std::isprint(static_cast<unsigned char>(ch)) ? ch : '.');
        }
    }

    std::cout << std::endl;
}
