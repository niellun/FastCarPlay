#ifndef SRC_PCM_AUDIO
#define SRC_PCM_AUDIO

#include <iostream>
#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "helper/error.h"

#define FADE_IN_SPEED 0.00001
#define FADE_OUT_SPEED 0.0001

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
    PcmAudio(const char *name = "");
    ~PcmAudio();

    // Start playing raw PCM data from queue
    void start(AtomicQueue<Message> *data, PcmAudio *fader = nullptr);
    void stop();

    void Fade(bool enble);

private:
    void runner();
    void loop(SDL_AudioDeviceID device);
    void fadecpy(uint8_t *target, uint8_t *source, size_t len);

    static void callback(void *userdata, Uint8 *stream, int len);
    static ChannelConfig _configTable[];
    static ChannelConfig getConfig(int type);

    AtomicQueue<Message> *_data;
    ChannelConfig _config;
    int _offset;
    PcmAudio *_fader;
    bool _fade;
    bool _faded;
    float _volume;
    float _fadeVolume;

    std::thread _thread;
    std::mutex _mtx;
    std::condition_variable _cv;
    std::atomic<bool> _reconfig{false};
    std::atomic<bool> _active{false};
    std::string _name;
};

#endif /* SRC_PCM_AUDIO */
