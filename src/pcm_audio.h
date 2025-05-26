#ifndef SRC_PCM_AUDIO
#define SRC_PCM_AUDIO

#include <iostream>
#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "helper/error.h"

#define NO_DATA_FRAMES 20

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
    void runner();
    void loop(SDL_AudioDeviceID device);

    static void callback(void *userdata, Uint8 *stream, int len);
    static ChannelConfig _configTable[];
    static ChannelConfig getConfig(int type);

    float _volume = 1.0f;

    AtomicQueue<Message> *_data;
    ChannelConfig _config;
    int _offset;
    int _nodata;

    std::thread _thread;
    std::mutex _mtx;
    std::condition_variable _cv;
    std::atomic<bool> _reconfig{false};
    std::atomic<bool> _active{false};
};

#endif /* SRC_PCM_AUDIO */
