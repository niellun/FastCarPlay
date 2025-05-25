#ifndef SRC_STRUCT_MESSAGE
#define SRC_STRUCT_MESSAGE

#include <cstdint>
#include <cstring>
#include <memory>
#include <cstdlib>

#define OFFSET_AUDIO_FORMAT 0

class Message
{
public:
    Message(uint8_t *data, uint32_t data_length, uint32_t offset) : _data(data), _length(data_length), _offset(offset)
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

    int getInt(uint32_t offset) const
    {
        int result = 0;
        if (_length - sizeof(int) >= offset)
            memcpy(&result, _data + offset, sizeof(int));
        return result;
    }

    uint8_t *data() const { return _data + _offset; }
    uint32_t length() const { return _length - _offset; }

private:
    uint8_t *_data;
    uint32_t _length;
    uint32_t _offset;
};

#endif /* SRC_STRUCT_MESSAGE */
