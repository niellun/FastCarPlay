#ifndef SRC_RECORDER
#define SRC_RECORDER

#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "helper/iaudio_sender.h"
#include "struct/atomic_queue.h"

class AudioChunk
{
public:
    AudioChunk(uint16_t size)
        : data(new uint8_t[size]), size(size)
    {
    }

    ~AudioChunk()
    {
        delete[] data;
    }

    // Deleted copy constructor/assignment
    AudioChunk(const AudioChunk &) = delete;
    AudioChunk &operator=(const AudioChunk &) = delete;

    uint8_t *data;
    size_t size;
};

class Recorder
{
public:
    Recorder(uint16_t buffSize);
    ~Recorder();

    void start(IAudioSender *sender);
    void stop();

private:
    static void AudioCallback(void *userdata, Uint8 *stream, int len);
    void runner();

    IAudioSender *_sender;
    std::atomic<bool> _active;
    std::thread _thread;
    SDL_AudioDeviceID _device;
    AtomicQueue<AudioChunk> _data;
};

#endif /* SRC_RECORDER */
