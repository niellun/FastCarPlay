#include "protocol.h"

#include <cstring>

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
    switch (cmd)
    {
    case CMD_OPEN:
        return "Open";
    case CMD_PLUGGED:
        return "Plugged";
    case CMD_UNPLUGGED:
        return "Unplugged";
    case CMD_TOUCH:
        return "Touch";
    case CMD_VIDEO_DATA:
        return "Video";
    case CMD_AUDIO_DATA:
        return "Audio";
    case CMD_SEND_FILE:
        return "File";
    default:
        return "Unknown";
    }
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
    Connector::write_uint32_le(&buf[0], width);
    Connector::write_uint32_le(&buf[4], height);
    Connector::write_uint32_le(&buf[8], fps);
    Connector::write_uint32_le(&buf[12], 5);
    Connector::write_uint32_le(&buf[16], 49152);
    Connector::write_uint32_le(&buf[20], 2);
    Connector::write_uint32_le(&buf[24], 2);

    connector.send(1, buf, 28);
}

void Protocol::sendKey(int key)
{
    printf("Send key %d", key);

    uint8_t buf[4];
    Connector::write_uint32_le(&buf[0], key);

    connector.send(8, buf, 4);
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
    Connector::write_uint32_le(buf, value);
    sendFile(filename, buf, 4);
}

void Protocol::sendClick(float x, float y, bool down)
{
    uint8_t buf[16];
    Connector::write_uint32_le(buf, down ? 14 : 16);
    Connector::write_uint32_le(buf + 4, int(10000 * x));
    Connector::write_uint32_le(buf + 8, int(10000 * y));
    Connector::write_uint32_le(buf + 12, 0);
    connector.send(5, buf, 16);
}

void Protocol::sendMove(float dx, float dy)
{
    uint8_t buf[16];
    Connector::write_uint32_le(buf, 15);
    Connector::write_uint32_le(buf + 4, int(10000 * dx));
    Connector::write_uint32_le(buf + 8, int(10000 * dy));
    Connector::write_uint32_le(buf + 12, 0);
    connector.send(5, buf, 16);
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
    Connector::write_uint32_le(buf, fn_len);
    buf += 4;

    // 2) filename bytes (including the '\0')
    std::memcpy(buf, filename, fn_len);
    buf += fn_len;

    // 3) content length (LE)
    Connector::write_uint32_le(buf, length);
    buf += 4;

    // 4) content bytes
    std::memcpy(buf, data, length);

    connector.send(CMD_SEND_FILE, result.data(), total);
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
        sendInit(_width, _height, _fps);
        sendFile("/tmp/night_mode", 0);      // 0==day, 1==night
        sendFile("/tmp/hand_drive_mode", 0); // 0==left, 1==right
        sendFile("/tmp/charge_mode", 0);
        sendFile("/etc/box_name", "CarPlay");
    }
    else
    {
    }
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
        videoData.pushDiscard( std::make_unique<Message>(data, length, 20));
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
        phoneConnected = true;
        break;
    }
    case CMD_UNPLUGGED:
    {
        phoneConnected = false;
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
        printf("  > ");
        size_t count = length / 4;
        for (size_t i = 0; (i < count) & (i < max); ++i)
        {
            uint32_t val =
                ((uint32_t)data[i * 4 + 0]) |
                ((uint32_t)data[i * 4 + 1] << 8) |
                ((uint32_t)data[i * 4 + 2] << 16) |
                ((uint32_t)data[i * 4 + 3] << 24);
            printf("%u ", val);
        }
        printf("\n");
    }
}

void Protocol::print_bytes(uint32_t length, uint8_t *data, uint16_t max)
{
    if (data && length >= 4)
    {
        printf("  > ");
        for (size_t i = 0; (i < length) & (i < max); ++i)
        {
            printf("%d ", data[i]);
        }
        printf("\n");
    }
}

void Protocol::print_message(uint32_t cmd, uint32_t length, uint8_t *data)
{
    printf("Cmd: %-3u %-10s Size: %-6u > ", cmd, cmdString(cmd), length);
    if (data && length > 0)
        for (size_t i = 0; i < 40 && i < length; ++i)
        {
            char ch = static_cast<char>(data[i]);
            printf("%c", isprint(ch) ? ch : '.');
        }
    // for (int i = 0; i < length && i < 10; i++)
    //     printf("%-4u", data[i]);
    printf("\n");
}
