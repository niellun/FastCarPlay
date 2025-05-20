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
    if (vol < 0)
    {
        _volume = 0;
        return;
    }
    if (vol > 1)
    {
        _volume = 1;
        return;
    }
    _volume = vol;
}

void PcmAudio::start(AtomicQueue<Message> *data)
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

static ChannelConfig configTable[] = {
    {8000, 1},  // type = 3
    {48000, 2}, // type = 4
    {16000, 1}, // type = 5
    {24000, 1}, // type = 6
    {16000, 2}, // type = 7
};

ChannelConfig PcmAudio::getConfig(int type) const
{
    if (type >= 3 && type <= 7)
        return configTable[type - 3];
    return {44100, 2};
}

void PcmAudio::runner()
{
    setThreadName("audio");

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec;
    size_t bufferedBytes = 0;
    bool unpaused = false;
    size_t targetBytes = 0;
    ChannelConfig config = {0, 0};

    while (_active)
    {
        unique_ptr<Message> segment = _data->wait(_active);
        if (!_active)
            break;

        ChannelConfig segmentConfig = getConfig(segment->getInt(OFFSET_AUDIO_FORMAT));

        // If settings changed, (re)open audio device
        if (device == 0 || config != segmentConfig)
        {
            config = segmentConfig;

            // Close existing device
            if (device != 0)
            {
                SDL_CloseAudioDevice(device);
                device = 0;
            }
            // Configure new spec
            SDL_zero(spec);
            spec.freq = config.rate;
            spec.format = AUDIO_S16SYS;
            spec.channels = config.channels;
            spec.samples = 4096;
            spec.callback = nullptr;

            device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            if (device == 0)
            {
                std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
                continue;
            }
            // Calculate new buffer target: 0.5s
            targetBytes = config.rate * config.channels * sizeof(int16_t) / 2;
            bufferedBytes = 0;
            unpaused = false;
            // Start paused
            SDL_PauseAudioDevice(device, 1);
        }

        // Apply volume in-place
        int16_t *samples = reinterpret_cast<int16_t *>(segment->data());
        size_t count = segment->length() / sizeof(int16_t);
        for (size_t i = 0; i < count; ++i)
        {
            samples[i] = static_cast<int16_t>(samples[i] * _volume);
        }

        // Queue audio
        SDL_QueueAudio(device, segment->data(), segment->length());
        bufferedBytes += segment->length();

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
