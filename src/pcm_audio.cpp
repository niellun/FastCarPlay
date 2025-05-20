#include "pcm_audio.h"
#include "helper/functions.h"

// Implementation
PcmAudio::PcmAudio() {}

PcmAudio::~PcmAudio()
{
    stop();
}

void PcmAudio::setVolume(float vol)
{
    if(vol<0)
    {
        _volume = 0;
        return;
    }
    if(vol>1)
    {
        _volume = 1;
        return;
    }
    _volume = vol;
}

void PcmAudio::start(RawQueue *data)
{
    if (_active)
        stop();
    _data = data;
    _active = true;
    _thread = std::thread(&PcmAudio::runner, this);
}

void PcmAudio::stop()
{
    if (!_active)
        return;
    _active = false;
    _data->notify();
    if (_thread.joinable())
        _thread.join();
}

void PcmAudio::runner()
{
    setThreadName("audio");

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec;
    size_t bufferedBytes = 0;
    bool unpaused = false;
    size_t targetBytes = 0;
    int rate = 0;
    int channels = 0;

    while (_active)
    {
        RawEntry segment = _data->wait(_active);
        if (!_active)
        {
            if (segment.data)
                free(segment.data);
            break;
        }

        int config = 0;
        memcpy(&config, segment.data, sizeof(int));

        int newRate = 44100;
        int newChannels = 2;
        switch (config)
        {
        case 1:
            newRate = 44100;
            newChannels = 2;
            break;
        case 2:
            newRate = 44100;
            newChannels = 2;
            break;
        case 3:
            newRate = 8000;
            newChannels = 1;
            break;
        case 4:
            newRate = 48000;
            newChannels = 2;
            break;
        case 5:
            newRate = 16000;
            newChannels = 1;
            break;
        case 6:
            newRate = 24000;
            newChannels = 1;
            break;
        case 7:
            newRate = 16000;
            newChannels = 2;
            break;
        }

        // If settings changed, (re)open audio device
        if (device == 0 || rate != newRate || channels != newChannels)
        {
            rate = newRate;
            channels = newChannels;

            printf("PCM SETTING %d %d\n", rate, channels);
            // Close existing device
            if (device != 0)
            {
                SDL_CloseAudioDevice(device);
                device = 0;
            }
            // Configure new spec
            SDL_zero(spec);
            spec.freq = rate;
            spec.format = AUDIO_S16SYS;
            spec.channels = static_cast<Uint8>(channels);
            spec.samples = 4096;
            spec.callback = nullptr;

            device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            if (device == 0)
            {
                std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
                free(segment.data);
                continue;
            }
            // Calculate new buffer target: 0.5s
            targetBytes = rate * channels * sizeof(int16_t) / 2;
            bufferedBytes = 0;
            unpaused = false;
            // Start paused
            SDL_PauseAudioDevice(device, 1);
        }

        // Apply volume in-place
        int16_t *samples = reinterpret_cast<int16_t *>(segment.data + segment.offset);
        size_t count = segment.size / sizeof(int16_t);
        for (size_t i = 0; i < count; ++i)
        {
            samples[i] = static_cast<int16_t>(samples[i] * _volume);
        }

        // Queue audio
        SDL_QueueAudio(device, segment.data + segment.offset, segment.size);
        bufferedBytes += segment.size;
        free(segment.data);

        // Unpause when enough buffered
        if (!unpaused && bufferedBytes >= targetBytes)
        {
            SDL_PauseAudioDevice(device, 0);
            unpaused = true;
        }
    }

    if (device)
        SDL_CloseAudioDevice(device);
}
