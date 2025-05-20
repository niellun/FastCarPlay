#ifndef SRC_PCM_AUDIO
#define SRC_PCM_AUDIO

#include <iostream>
#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "helper/error.h"

struct ChannelConfig
{
    int rate;
    uint8_t channels;

    bool operator==(ChannelConfig const &other) const
    {
        return rate == other.rate && channels == other.channels;
    }

    bool operator!=(ChannelConfig const &other) const
    {
        return !(*this == other);
    }
};

class PcmAudio
{
public:
    PcmAudio();
    ~PcmAudio();

    // Start playing raw PCM data from queue
    void start(AtomicQueue<Message> *data);
    void stop();

    void setVolume(float vol);

private:
    ChannelConfig getConfig(int type) const;
    void runner();
    void loop(SDL_AudioDeviceID device);

    AtomicQueue<Message> *_data = nullptr;

    float _volume = 1.0f;

    std::thread _thread;
    std::atomic<bool> _active{false};
};

#endif /* SRC_PCM_AUDIO */
