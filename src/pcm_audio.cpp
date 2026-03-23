#include "pcm_audio.h"
#include "common/functions.h"
#include "protocol/protocol_const.h"
#include "settings.h"
#include "common/logger.h"

// Add sample size (buffer size in samples) to ChannelConfig
ChannelConfig PcmAudio::_configTable[] = {
    {8000, 1, 1},   // type = 3, ~256ms
    {48000, 2, 4},  // type = 4, ~170ms
    {16000, 1, 2},  // type = 5, ~256ms
    {24000, 1, 2},  // type = 6, ~170ms
    {16000, 2, 2},  // type = 7, ~256ms
};

// Implementation
PcmAudio::PcmAudio(const char *name)
{
    if (!name || strlen(name) < 1)
    {
        _name = "[Audio]";
        return;
    }
    _name = name;
    _fade = false;
    _faded = false;
    _volume = 1.0;
    _fadeVolume = Settings::audioFade;
    _config.channels = 0;
    _config.rate = 0;
    if (_fadeVolume < 0)
        _fadeVolume = 0;
    if (_fadeVolume > 1)
        _fadeVolume = 1;
}

PcmAudio::~PcmAudio()
{
    stop();
    if (_thread.joinable())
        _thread.join();
}

void PcmAudio::fadecpy(uint8_t *target, uint8_t *source, size_t len)
{
    if (!_fade && !_faded)
    {
        std::memcpy(target, source, len);
        return;
    }

    int16_t *src = reinterpret_cast<int16_t *>(source);
    int16_t *dst = reinterpret_cast<int16_t *>(target);
    _faded = true;
    for (size_t i = 0; i < len / 2; i++)
    {
        if (_fade)
        {
            if (_volume - FADE_OUT_SPEED >= _fadeVolume)
                _volume = _volume - FADE_OUT_SPEED;
        }
        else
        {
            if (_volume + FADE_IN_SPEED <= 1)
                _volume = _volume + FADE_IN_SPEED;
        }
        dst[i] = src[i] * _volume;
    }
    _faded = _volume + FADE_IN_SPEED <= 1;
}

void PcmAudio::callback(void *userdata, Uint8 *stream, int len)
{
    PcmAudio *self = static_cast<PcmAudio *>(userdata);
    const Message *segment = self->_data->peek();
    if (segment && self->_offset == 0 && getConfig(segment->getInt(OFFSET_AUDIO_FORMAT)) != self->_config)
    {
        self->_paused = true;
        self->_cv.notify_one();
    }

    if (self->_underflow)
    {
        int count = self->_data->count();
        if (count > self->_prefill)
            self->_underflow = false;
        else if (count == self->_lastCount)
            self->_underflowCount++;
        else
            self->_lastCount = count;
    }

    while (len > 0)
    {
        if(segment == nullptr && !self->_underflow)
        {
            if (self->_fader)
                self->_fader->Fade(false);
            self->_underflowCount = 0;
            self->_underflow = true;
        }

        if (self->_underflow)
        {
            std::fill_n(stream, len, 0);
            self->_faded = self->_fade;
            self->_volume = self->_faded ? Settings::audioFade : 1;
            if (self->_underflowCount < BUFFER_WAIT_COUNT)
                return;
            self->_data->clear();
            self->_paused = true;
            self->_cv.notify_one();
            return;
        }

        int remain = segment->length() - self->_offset;
        uint8_t *data = segment->data() + self->_offset;

        if (remain > len)
        {
            self->fadecpy(stream, data, len);
            self->_offset = self->_offset + len;
            return;
        }

        self->fadecpy(stream, data, remain);
        len = len - remain;
        stream = stream + remain;
        self->_data->pop();
        self->_offset = 0;
        segment = self->_data->peek();
        if (segment && getConfig(segment->getInt(OFFSET_AUDIO_FORMAT)) != self->_config)
        {
            self->_paused = true;
            self->_cv.notify_one();
        }
    }
}

ChannelConfig PcmAudio::getConfig(int type)
{
    if (type >= 3 && type <= 7)
        return _configTable[type - 3];
    // Default: 44100Hz, stereo, 4096 samples
    return {44100, 2, 4};
}

void PcmAudio::Fade(bool enable)
{
    _fade = enable;
}

void PcmAudio::start(AtomicQueue<Message> *data, PcmAudio *fader)
{
    if (_active)
        stop();
    _fader = fader;
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
    _cv.notify_all();
}

void PcmAudio::runner()
{
    std::string threadName = "audio-" + _name;
    setThreadName(threadName.c_str());

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec;

    while (_data->wait(_active))
    {
        const Message *segment = _data->peek();
        if (!segment)
            continue;

        ChannelConfig config = getConfig(segment->getInt(OFFSET_AUDIO_FORMAT));
        if (_config != config)
        {
            if (device != 0)
            {
                SDL_PauseAudioDevice(device, 1);
                SDL_CloseAudioDevice(device);
                device = 0;
            }

            // Configure new spec
            SDL_zero(spec);
            spec.freq = config.rate;
            spec.format = AUDIO_S16SYS;
            spec.channels = config.channels;
            spec.samples = Settings::audioBuffer*config.scale;
            spec.callback = callback;
            spec.userdata = this;

            device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            if (device == 0)
            {
                log_w("Failed to open audio %s > %s", _name.c_str(), SDL_GetError());                
                SDL_Delay(100);
                continue;
            }
            _config = config;
        }

        _offset = 0;
        _paused = false;
        _underflow = true;
        _underflowCount = 0;
        _lastCount = 0;
        _prefill = spec.channels == 1 ? Settings::audioDelayCall : Settings::audioDelay;

        SDL_PauseAudioDevice(device, 0);
        log_i("Start playing %s %dkHz %s", _name.c_str(), _config.rate, (_config.channels == 2 ? "stereo" : "mono"));
        if (_fader)
            _fader->Fade(true);

        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [&]
                 { return _paused.load() || !_active.load(); });

        SDL_PauseAudioDevice(device, 1);
        log_i("Stop playing %s", _name.c_str());
        if (_fader)
            _fader->Fade(false);
    }

    if (device)
    {
        SDL_PauseAudioDevice(device, 1);
        SDL_CloseAudioDevice(device);
    }
}
