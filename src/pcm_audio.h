#ifndef SRC_PCM_AUDIO
#define SRC_PCM_AUDIO

#include <iostream>
#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "struct/atomic_queue.h"
#include "protocol/message.h"

#define FADE_IN_SPEED 0.00001
#define FADE_OUT_SPEED 0.0001
#define FADE_ZERO_SEGMENTS 10
#define AUDIO_RESET_SECONDS 5

struct ChannelConfig
{
    int rate;
    uint8_t channels;
    uint8_t scale;

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

private:
    static ChannelConfig getConfig(const Message *msg);
    static ChannelConfig _configTable[];

    void fade(bool enble);
    void loop();
    void play(SDL_AudioDeviceID device, ChannelConfig config, int32_t segmentSize);
    bool isZero(const Message *msg);
    void fade(uint8_t *data, int32_t length);
    bool faded() const { return _volume <= _fadedVolume; }

    std::string _name;
    PcmAudio *_fader;
    std::thread _thread;
    std::atomic<bool> _playing;
    std::atomic<bool> _active;
    std::atomic<bool> _fade;
    ChannelConfig _config;
    AtomicQueue<Message> *_data;
    float _volume;
    float _fadedVolume;
};

#endif /* SRC_PCM_AUDIO */
