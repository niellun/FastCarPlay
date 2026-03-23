#ifndef SRC_PROTOCOL_MESSAGE
#define SRC_PROTOCOL_MESSAGE

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "protocol/aes_cipher.h"
#include "protocol/protocol_const.h"
#include "struct/multitouch.h"

#define MESSAGE_MAX_PAYLOAD_SIZE (2 * 1024 * 1024)

#pragma pack(push, 1)
struct Header
{
    uint32_t magic;
    int32_t length;
    uint32_t type;
    uint32_t typecheck;
};
#pragma pack(pop)

class Message
{
public:
    Message()
        : _header({0, 0, 0, 0}), _data(nullptr), _offset(0), _size(0), _encrypt(false)
    {
    }

    Message(uint32_t cmd, bool encrypt = true, int32_t size = 0, uint8_t *buffer = nullptr)
        : _header({static_cast<uint32_t>(MAGIC), size, cmd, ~cmd}),
          _data(nullptr),
          _offset(0),
          _size(0),
          _encrypt(encrypt)
    {
        if (size <= 0)
            return;

        if (!buffer)
        {
            allocate();
            return;
        }

        _data = buffer;
        _size = size;
    }

    Message(uint32_t cmd, uint32_t value, bool encrypt = true)
        : Message(cmd, encrypt, 4)
    {
        write_uint32_le(_data, value);
    }

    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;

    ~Message()
    {
        if (_data)
        {
            free(_data);
            _data = nullptr;
        }
    }

    uint8_t *allocate(uint32_t padding = 0)
    {
        if (_data != nullptr || _header.length <= 0)
            return nullptr;

        _size = _header.length + padding;
        _data = static_cast<uint8_t *>(malloc(_size));
        if (!_data)
            _size = 0;
        else
            std::fill(_data + _header.length, _data + _header.length + padding, 0);
        return _data;
    }

    int getInt(uint32_t offset) const
    {
        int result = 0;
        if (_data && offset + sizeof(int) <= _size)
            memcpy(&result, _data + offset, sizeof(int));
        return result;
    }

    bool setOffset(uint32_t offset)
    {
        if (offset >= _size)
            return false;
        _offset = offset;
        return true;
    }

    bool valid() const
    {
        if (_header.magic != MAGIC_ENC && _header.magic != MAGIC)
            return false;
        if (_header.typecheck != ~_header.type)
            return false;
        if (_header.length < 0 || _header.length > MESSAGE_MAX_PAYLOAD_SIZE)
            return false;
        return true;
    }

    Status encrypt(AESCipher *cipher)
    {
        if (!_encrypt)
            return Status::Success();

        if (_header.magic == MAGIC_ENC)
            return Status::Success();

        if (!cipher)
            return Status::Error("Cipher is not initialised");

        if (!allocated())
            return Status::Error("Message data is not allocated");

        Status result = cipher->Encrypt(_data, _header.length);

        if (result.succeed())
            _header.magic = MAGIC_ENC;

        return result;
    }

    Status decrypt(AESCipher *cipher)
    {
        if (_header.magic != MAGIC_ENC)
            return Status::Success();

        if (!cipher)
            return Status::Error("Cipher is not initialised");

        if (!allocated())
            return Status::Error("Message data is not allocated");

        return cipher->Decrypt(_data, _header.length);
    }

    bool isMotion() const
    {
        return _header.type == CMD_TOUCH && getInt(0) == 15;
    }

    static std::unique_ptr<Message> Init(int width, int height, int fps)
    {
        std::unique_ptr<Message> result(new Message(CMD_OPEN, true, 28));
        write_uint32_le(result->_data + 0, width);
        write_uint32_le(result->_data + 4, height);
        write_uint32_le(result->_data + 8, fps);
        write_uint32_le(result->_data + 12, 5);
        write_uint32_le(result->_data + 16, 49152);
        write_uint32_le(result->_data + 20, 2);
        write_uint32_le(result->_data + 24, 2);
        return result;
    }

    static std::unique_ptr<Message> File(const char *filename, const uint8_t *data, uint32_t length)
    {
        // filename is assumed null‑terminated, so strlen + 1 to include the '\0'
        uint32_t fn_len = strlen(filename) + 1;

        // Total buffer size: 4 (fn_len) + fn_len + 4 (content_len) + content_len
        std::unique_ptr<Message> result(new Message(CMD_SEND_FILE, true, 4 + fn_len + 4 + length));
        uint8_t *buf = result->_data;

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

    static std::unique_ptr<Message> File(const char *filename, const char *value)
    {
        uint32_t len = std::strlen(value);
        if (len > 16)
            throw std::invalid_argument("String too long (max 16 bytes)");
        // note: we send only the ASCII bytes, no trailing '\0'
        return File(filename, reinterpret_cast<const uint8_t *>(value), len);
    }

    // overload for a single 32‑bit integer
    static std::unique_ptr<Message> File(const char *filename, int value)
    {
        uint8_t buffer[4];
        write_uint32_le(buffer, value);
        return File(filename, buffer, 4);
    }

    static std::unique_ptr<Message> Control(uint32_t value, bool encrypt = false)
    {
        return std::unique_ptr<Message>(new Message(CMD_CONTROL, value, encrypt));
    }

    static std::unique_ptr<Message> Encryption(uint32_t seed)
    {
        return std::unique_ptr<Message>(new Message(CMD_ENCRYPTION, seed, false));
    }

    static std::unique_ptr<Message> String(uint32_t cmd, const char *str, bool encrypt = true)
    {
        uint32_t length = std::strlen(str);
        std::unique_ptr<Message> result(new Message(cmd, encrypt, length));
        if (length > 0)
            std::memcpy(result->_data, str, length);
        return result;
    }

    template <typename... Args>
    static std::unique_ptr<Message> String(uint32_t cmd, const char *format, Args... args)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        return String(cmd, buffer);
    }

    static std::unique_ptr<Message> Touch(uint32_t action, float x, float y)
    {
        std::unique_ptr<Message> result(new Message(CMD_TOUCH, false, 16));
        write_uint32_le(result->_data, action);
        write_uint32_le(result->_data + 4, int(10000 * x));
        write_uint32_le(result->_data + 8, int(10000 * y));
        write_uint32_le(result->_data + 12, 0);
        return result;
    }

    static std::unique_ptr<Message> Click(float x, float y, bool down)
    {
        return Touch(down ? 14 : 16, x, y);
    }

    static std::unique_ptr<Message> Move(float x, float y)
    {
        return Touch(15, x, y);
    }

    static std::unique_ptr<Message> Audio(int length)
    {
        std::unique_ptr<Message> result(new Message(CMD_AUDIO_DATA, false, length + AUDIO_BUFFER_OFFSET));
        write_uint32_le(result->_data, 5);
        write_uint32_le(result->_data + 4, 0);
        write_uint32_le(result->_data + 8, 3);
        result->setOffset(AUDIO_BUFFER_OFFSET);
        return result;
    }

    static std::unique_ptr<Message> MultiTouch(const Multitouch &touches)
    {
        int count = touches.size();
        if (count == 0)
            return nullptr;

        std::unique_ptr<Message> result(new Message(CMD_MULTI_TOUCH, false, sizeof(Multitouch::Touch) * count));
        uint8_t *buf = result->_data;
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

    static std::unique_ptr<Message> HeartBeat()
    {
        return std::unique_ptr<Message>(new Message(CMD_HEARTBEAT, false));
    }

    bool allocated() const { return _header.length <= 0 || static_cast<uint32_t>(_header.length) <= _size; }
    bool encrypted() const { return _header.magic == MAGIC_ENC; }
    uint8_t *header() { return reinterpret_cast<uint8_t *>(&_header); }
    uint32_t headerSize() const { return sizeof(Header); }
    uint32_t type() const { return _header.type; }
    int32_t length() const { return _header.length - _offset; }
    uint8_t *data() const { return _data ? _data + _offset : nullptr; }

private:
    static inline void write_uint32_le(uint8_t *dst, uint32_t value)
    {
        dst[0] = value & 0xFF;
        dst[1] = (value >> 8) & 0xFF;
        dst[2] = (value >> 16) & 0xFF;
        dst[3] = (value >> 24) & 0xFF;
    }

    static inline void write_float_le(uint8_t *dst, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        write_uint32_le(dst, bits);
    }

    Header _header;
    uint8_t *_data;
    uint32_t _offset;
    uint32_t _size;
    bool _encrypt;
};

#endif /* SRC_PROTOCOL_MESSAGE */
