#ifndef SRC_STRUCT_COMMAND
#define SRC_STRUCT_COMMAND

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>

#include "helper/functions.h"
#include "helper/protocol_const.h"
#include "struct/audio_chunk.h"
#include "struct/multitouch.h"

class Command
{
public:
    Command(const Command &) = delete;
    Command &operator=(const Command &) = delete;

    Command(int cmd, bool encrypt = true, uint32_t size = 0)
        : command(cmd), encryption(encrypt), length(size), data(nullptr)
    {
        if (size > 0)
            data = static_cast<uint8_t *>(malloc(size));
    }

    Command(int cmd, uint32_t value, bool encrypt = true)
        : Command(cmd, encrypt, 4)
    {
        write_uint32_le(data, value);
    }

    Command(int cmd, bool encrypt, uint8_t *buffer, uint32_t size)
        : command(cmd), encryption(encrypt), length(size), data(buffer)
    {
    }

    ~Command()
    {
        if (data)
        {
            free(data);
            data = nullptr;
        }
    }

    static std::unique_ptr<Command> Init(int width, int height, int fps)
    {
        std::unique_ptr<Command> result(new Command(CMD_OPEN, true, 28));
        write_uint32_le(result->data + 0, width);
        write_uint32_le(result->data + 4, height);
        write_uint32_le(result->data + 8, fps);
        write_uint32_le(result->data + 12, 5);
        write_uint32_le(result->data + 16, 49152);
        write_uint32_le(result->data + 20, 2);
        write_uint32_le(result->data + 24, 2);
        return result;
    }

    static std::unique_ptr<Command> File(const char *filename, const uint8_t *data, uint32_t length)
    {
        // filename is assumed null‑terminated, so strlen + 1 to include the '\0'
        uint32_t fn_len = strlen(filename) + 1;

        // Total buffer size: 4 (fn_len) + fn_len + 4 (content_len) + content_len
        std::unique_ptr<Command> result(new Command(CMD_SEND_FILE, true, 4 + fn_len + 4 + length));
        uint8_t *buf = result->data;

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
        if (length > 0 && data)
            std::memcpy(buf, data, length);

        return result;
    }

    static std::unique_ptr<Command> File(const char *filename, const char *value)
    {
        uint32_t len = std::strlen(value);
        if (len > 16)
            throw std::invalid_argument("String too long (max 16 bytes)");
        // note: we send only the ASCII bytes, no trailing '\0'
        return File(filename, reinterpret_cast<const uint8_t *>(value), len);
    }

    // overload for a single 32‑bit integer
    static std::unique_ptr<Command> File(const char *filename, int value)
    {
        uint8_t buffer[4];
        write_uint32_le(buffer, value);
        return File(filename, buffer, 4);
    }

    static std::unique_ptr<Command> Control(uint32_t value, bool encrypt = false)
    {
        return std::unique_ptr<Command>(new Command(CMD_CONTROL, value, encrypt));
    }

    static std::unique_ptr<Command> Encryption(uint32_t seed)
    {
        return std::unique_ptr<Command>(new Command(CMD_ENCRYPTION, seed, false));
    }

    static std::unique_ptr<Command> String(uint32_t cmd, const char *str, bool encrypt = true)
    {
        uint32_t length = std::strlen(str);
        std::unique_ptr<Command> result(new Command(cmd, encrypt, length));
        if (length > 0)
            std::memcpy(result->data, str, length);
        return result;
    }

    template <typename... Args>
    static std::unique_ptr<Command> String(uint32_t cmd, const char *format, Args... args)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        return String(cmd, buffer);
    }

    static std::unique_ptr<Command> Touch(uint32_t action, float x, float y)
    {
        std::unique_ptr<Command> result(new Command(CMD_TOUCH, false, 16));
        write_uint32_le(result->data, action);
        write_uint32_le(result->data + 4, int(10000 * x));
        write_uint32_le(result->data + 8, int(10000 * y));
        write_uint32_le(result->data + 12, 0);
        return result;
    }

    static std::unique_ptr<Command> Click(float x, float y, bool down)
    {
        return Touch(down ? 14 : 16, x, y);
    }

    static std::unique_ptr<Command> Move(float x, float y)
    {
        return Touch(15, x, y);
    }

    static std::unique_ptr<Command> Audio(std::unique_ptr<AudioChunk> chunk)
    {
        std::unique_ptr<Command> result(new Command(CMD_AUDIO_DATA, false, chunk->data, chunk->size));
        chunk->data = nullptr;
        write_uint32_le(result->data, 5);
        write_uint32_le(result->data + 4, 0);
        write_uint32_le(result->data + 8, 3);
        return result;
    }

    static std::unique_ptr<Command> MultiTouch(const Multitouch &touches)
    {
        int count = touches.size();
        if (count == 0)
            return nullptr;

        std::unique_ptr<Command> result(new Command(CMD_MULTI_TOUCH, false, sizeof(Multitouch::Touch) * count));
        uint8_t *buf = result->data;
        for (int i = 0; i < count; ++i)
        {
            const Multitouch::Touch &t = touches[i];
            write_float_le(buf + 0, t.x);
            write_float_le(buf + 4, t.y);
            write_uint32_le(buf + 8, static_cast<uint32_t>(t.state));
            write_uint32_le(buf + 12, static_cast<uint32_t>(t.id));
            buf += 16;
        }
        return result;
    }

    int command;
    bool encryption;
    uint32_t length;
    uint8_t *data;
};

#endif /* SRC_STRUCT_COMMAND */
