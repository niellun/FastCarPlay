#ifndef SRC_STRUCT_MESSAGE
#define SRC_STRUCT_MESSAGE

#include "libavcodec/defs.h"
#include "helper/protocol_const.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <cstdlib>

#include "aes_cipher.h"

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
    Message() : _header({0, 0, 0, 0}), _data(nullptr), _offset(0), _headerLegth(0), _dataLength(0)
    {
    }

    ~Message()
    {
        if (_data)
        {
            free(_data);
            _data = nullptr;
        }
    }

    uint32_t parse(uint8_t *data, uint32_t data_length)
    {
        uint32_t result = 0;

        if (!hasHeader())
        {
            uint8_t copy = sizeof(Header) - _headerLegth;
            if (copy > data_length)
                copy = data_length;
            memcpy(reinterpret_cast<uint8_t *>(&_header) + _headerLegth, data, copy);
            _headerLegth += copy;
            result += copy;
        }

        if (!hasHeader() || result >= data_length || _header.length <= 0)
            return result;

        if (_data == nullptr)
        {
            uint32_t padding = _header.type == CMD_VIDEO_DATA ? AV_INPUT_BUFFER_PADDING_SIZE : 0;
            _data = (uint8_t *)malloc(_header.length + padding);
            if (padding > 0 && _data)
                std::fill(_data + _header.length, _data + _header.length + padding, 0);
        }

        uint32_t copy = _header.length - _dataLength;
        if (copy > data_length - result)
            copy = data_length - result;
        if (_data)
            memcpy(_data + _dataLength, data + result, copy);
        _dataLength += copy;
        result += copy;

        return result;
    }

    int getInt(uint32_t offset) const
    {
        int result = 0;
        if (_data && _dataLength - sizeof(int) >= offset)
            memcpy(&result, _data + offset, sizeof(int));
        return result;
    }

    bool ready() const { return _headerLegth == sizeof(Header) && (_header.length <= 0 || _dataLength >= _header.length); }

    bool valid() const { return _header.length <= 0 || _data; }

    bool setOffset(uint32_t offset)
    {
        if (offset >= _dataLength)
            return false;
        _offset = offset;
        return true;
    }

    bool encrypted() const { return _header.magic == MAGIC_ENC; }

    bool decrypt(AESCipher *cipher)
    {
        if (!cipher)
            return false;
        return cipher->Decrypt(_data, _dataLength);
    }

    uint8_t *data() const { return _data + _offset; }
    uint32_t length() const { return _dataLength - _offset; }
    uint32_t type() const { return _header.type; }

private:
    bool hasHeader() const { return _headerLegth == sizeof(Header); }

    Header _header;
    uint8_t *_data;
    uint32_t _offset;

    u_int8_t _headerLegth;
    uint32_t _dataLength;
};

#endif /* SRC_STRUCT_MESSAGE */
