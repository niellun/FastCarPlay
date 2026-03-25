#include "pcm_audio.h"
#include "common/functions.h"
#include "protocol/protocol_const.h"
#include "settings.h"
#include "common/logger.h"
#include <time.h>

// Add sample size (buffer size in samples) to ChannelConfig
ChannelConfig PcmAudio::_configTable[] = {
    {8000, 1, 1},  // type = 3, ~256ms
    {48000, 2, 4}, // type = 4, ~170ms
    {16000, 1, 2}, // type = 5, ~256ms
    {24000, 1, 2}, // type = 6, ~170ms
    {16000, 2, 2}, // type = 7, ~256ms
};

PcmAudio::PcmAudio(const char *name) : _name("default"),
                                       _fader(nullptr),
                                       _playing(false),
                                       _active(false),
                                       _fade(false),
                                       _config({0, 0, 0}),
                                       _volume(1),
                                       _fadedVolume(Settings::audioFade)
{
    if (name && strlen(name) > 0)
        _name = name;
    log_v("Created %s", _name.c_str());
}

PcmAudio::~PcmAudio()
{
    stop();
    if (_thread.joinable())
        _thread.join();
    log_v("Destroyed %s", _name.c_str());
}

void PcmAudio::start(AtomicQueue<Message> *data, PcmAudio *fader)
{
    if (_active)
        stop();
    log_v("Starting %s", _name.c_str());
    _fader = fader;
    _data = data;
    _active = true;
    _thread = std::thread(&PcmAudio::loop, this);
}

void PcmAudio::stop()
{
    if (!_active)
        return;
    log_v("Stopping %s", _name.c_str());
    _active = false;
    _data->notify();
}

ChannelConfig PcmAudio::getConfig(const Message *msg)
{
    uint8_t type = 0;
    if (msg)
    {
        type = msg->getInt(OFFSET_AUDIO_FORMAT);
    }

    if (type >= 3 && type <= 7)
        return _configTable[type - 3];
    // Default: 44100Hz, stereo, 4096 samples
    return {44100, 2, 4};
}

bool PcmAudio::isZero(const Message *msg)
{
    const uint64_t *p = (const uint64_t *)msg->data();
    int n = msg->length() / 8;
    for (int i = 0; i < n; ++i)
    {
        if (p[i] != 0)
            return false;
    }
    return true;
}

void PcmAudio::fade(bool enable)
{
    _fade.store(enable);
    if (!_playing)
        _volume = enable ? Settings::audioFade : 1.0;
}

void PcmAudio::fade(uint8_t *data, int32_t length)
{
    bool fade = _fade.load();
    if (!fade && _volume >= 1)
        return;

    int16_t *buf = reinterpret_cast<int16_t *>(data);
    for (int i = 0; i < length / 2; i++)
    {
        if (fade)
        {
            if (_volume - FADE_OUT_SPEED >= _fadedVolume)
                _volume = _volume - FADE_OUT_SPEED;
        }
        else
        {
            if (_volume + FADE_IN_SPEED <= 1)
                _volume = _volume + FADE_IN_SPEED;
        }
        if (_volume < 1)
            buf[i] = buf[i] * _volume;
    }
}

void PcmAudio::play(SDL_AudioDeviceID device, ChannelConfig config, int32_t segmentSize)
{
    uint8_t zeroSegments = 0;
    bool nonZero = false;

    int prefill = config.channels == 1 ? Settings::audioDelayCall : Settings::audioDelay;
    int segmentTimeMs = 1000.0 * segmentSize / (config.rate * config.channels * 2.0);
    int waitTimeMs = (prefill + 1) * segmentTimeMs;
    log_i("Prepare to play %s %dkHz %s chunk %d ~%dms prefill %d ~%dms", _name.c_str(),
          config.rate,
          (config.channels == 2 ? "stereo" : "mono"),
          segmentSize,
          segmentTimeMs,
          prefill,
          waitTimeMs);

    if (!_data->waitFor(_active, AUDIO_RESET_SECONDS * 1000, prefill))
    {
        _data->clear();
        log_w("Not enough data to play %s %dkHz %s chunk %d ~%dms prefill %d ~%dms",
              _name.c_str(),
              config.rate,
              (config.channels == 2 ? "stereo" : "mono"),
              segmentSize,
              segmentTimeMs,
              prefill,
              waitTimeMs);
        return;
    }

    if (_fader && !_fader->faded())
    {
        SDL_Delay(Settings::audioAuxDelay);
    }

    while (_active)
    {
        std::unique_ptr<Message> segment = _data->pop();
        if (!segment)
            return;

        char error[256];
        if (!segment->decrypt(error))
        {
            log_w("Can't decrypt audio segment > %s", error);
            continue;
        }

        if (config != getConfig(segment.get()))
            return;

        fade(segment->data(), segment->length());

        SDL_QueueAudio(device, segment->data(), segment->length());

        if (!_playing && prefill-- <= 0)
        {
            log_d("Start playing %s %dkHz %s",
                  _name.c_str(),
                  config.rate,
                  (config.channels == 2 ? "stereo" : "mono"));
            SDL_PauseAudioDevice(device, 0);
            _playing = true;
        }

        if (_fader)
        {
            if (isZero(segment.get()))
            {
                if (nonZero && ++zeroSegments == FADE_ZERO_SEGMENTS)
                {
                    log_d("Audio %s is zeroes, fade other channel in", _name.c_str());
                    _fader->fade(false);
                }
            }
            else
            {
                nonZero = true;
                zeroSegments = 0;
                _fader->fade(true);
            }
        }

        if (!_data->waitFor(_active, waitTimeMs))
            return;
    }
}

void PcmAudio::loop()
{
    std::string threadName = "audio-" + _name;
    setThreadName(threadName.c_str());

    log_d("Started thread %s", _name.c_str());

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec;

    time_t playEnd = time(NULL);
    while (_data->wait(_active))
    {
        const Message *segment = _data->peek();
        if (!segment)
            continue;

        ChannelConfig config = getConfig(segment);
        if (_config != config)
        {
            if (device != 0)
            {
                SDL_PauseAudioDevice(device, 1);
                SDL_ClearQueuedAudio(device);
                SDL_CloseAudioDevice(device);
                device = 0;
            }

            // Configure new spec
            SDL_zero(spec);
            spec.freq = config.rate;
            spec.format = AUDIO_S16SYS;
            spec.channels = config.channels;
            spec.samples = Settings::audioBuffer * config.scale;
            spec.callback = nullptr;
            spec.userdata = nullptr;

            device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            if (device == 0)
            {
                log_w("Failed to open audio %s %dkHz %s samples %d > %s",
                      _name.c_str(),
                      config.rate,
                      (config.channels == 2 ? "stereo" : "mono"),
                      Settings::audioBuffer * config.scale, SDL_GetError());
                SDL_Delay(100);
                continue;
            }
            _config = config;
        }

        if (difftime(time(NULL), playEnd) > AUDIO_RESET_SECONDS)
            SDL_ClearQueuedAudio(device);

        if (_fader)
            _fader->fade(true);
        play(device, config, segment->length());
        _playing = false;
        if (_fader)
            _fader->fade(false);
        SDL_PauseAudioDevice(device, 1);
        playEnd = time(NULL);
        log_d("Stop playing %s %dkHz %s",
              _name.c_str(),
              config.rate,
              (config.channels == 2 ? "stereo" : "mono"));
    }

    if (device != 0)
    {
        SDL_PauseAudioDevice(device, 1);
        SDL_ClearQueuedAudio(device);
        SDL_CloseAudioDevice(device);
    }

    log_v("Stopped thread %s", _name.c_str());
}
