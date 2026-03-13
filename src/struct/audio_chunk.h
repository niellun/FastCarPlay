#ifndef SRC_STRUCT_AUDIO_CHUNK
#define SRC_STRUCT_AUDIO_CHUNK

#include <cstdlib>
#include <cstddef>
#include <cstdint>

class AudioChunk
{
public:
    AudioChunk(uint16_t size)
        : data(nullptr), size(size)
    {
        if (size > 0)
        data =  static_cast<uint8_t *>(malloc(size));
    }

    ~AudioChunk()
    {
        free(data);
    }

    // Deleted copy constructor/assignment
    AudioChunk(const AudioChunk &) = delete;
    AudioChunk &operator=(const AudioChunk &) = delete;

    uint8_t *data;
    uint16_t size;
};

#endif /* SRC_STRUCT_AUDIO_CHUNK */