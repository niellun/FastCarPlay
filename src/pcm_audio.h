#ifndef SRC_PCM_AUDIO
#define SRC_PCM_AUDIO

#include <iostream>
#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "struct/raw_queue.h"
#include "helper/error.h"


class PcmAudio {
public:
    PcmAudio();
    ~PcmAudio();

    // Start playing raw PCM data from queue
    void start(RawQueue* data);
    void stop();

    void setVolume(float vol);

private:
    void runner();
    void loop(SDL_AudioDeviceID device);

    RawQueue* _data = nullptr;
    int _sampleRate = 0;
    int _channels = 0;

    float _volume = 1.0f;

    std::thread _thread;
    std::atomic<bool> _active{false};
};

#endif /* SRC_PCM_AUDIO */
