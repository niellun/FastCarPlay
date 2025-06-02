#include "pcm_audio.h"
#include "helper/functions.h"
#include "settings.h"

ChannelConfig PcmAudio::_configTable[] = {
    {8000, 1},  // type = 3
    {48000, 2}, // type = 4
    {16000, 1}, // type = 5
    {24000, 1}, // type = 6
    {16000, 2}, // type = 7
};

// Implementation
PcmAudio::PcmAudio(const char *name)
{
    if (!name || strlen(name) < 1)
    {
        _name = "[Audio]";
        return;
    }
    _name = "[Audio ";
    _name = _name + name + "]";
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
        self->_reconfig = true;

    bool underflow;
    if (self->_data->has(self->_underflowSize))
    {
        underflow = false;
        self->_underflowCount = 0;
    }
    else
    {
        underflow = true;
    }

    while (len > 0)
    {
        if (segment == nullptr || self->_reconfig || underflow)
        {
            std::fill_n(stream, len, 0);
            self->_faded = self->_fade;
            self->_volume = self->_faded ? Settings::audioFade : 1;
            if (underflow && self->_underflowCount++ < 20)
            {
                self->_data->clear();
                return;
            }
            self->_reconfig = true;
            self->_cv.notify_one();
            return;
        }

        int remain = segment->length() - self->_offset;
        uint8_t *data = segment->data() + self->_offset;

        if (remain > len)
        {
            if (self->_fade || self->_faded)
                self->fadecpy(stream, data, len);
            else
                std::memcpy(stream, data, len);
            self->_offset = self->_offset + len;
            return;
        }

        if (self->_fade || self->_faded)
            self->fadecpy(stream, data, remain);
        else
            std::memcpy(stream, data, remain);

        len = len - remain;
        stream = stream + remain;
        self->_data->pop();
        self->_offset = 0;
        segment = self->_data->peek();
        if (segment && getConfig(segment->getInt(OFFSET_AUDIO_FORMAT)) != self->_config)
        {
            self->_reconfig = true;
            self->_cv.notify_one();
        }
    }
}

ChannelConfig PcmAudio::getConfig(int type)
{
    if (type >= 3 && type <= 7)
        return _configTable[type - 3];
    return {44100, 2};
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
    setThreadName("audio");

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
            spec.samples = 4096;
            spec.callback = callback;
            spec.userdata = this;

            device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            if (device == 0)
            {
                std::cerr << _name << " Failed to open audio: " << SDL_GetError() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            _config = config;
        }

        _offset = 0;
        _reconfig = false;
        _underflowCount = 0;
        _underflowSize = spec.channels == 1 ? Settings::audioDelayCall : Settings::audioDelay;

        SDL_PauseAudioDevice(device, 0);
        std::cout << _name << " Start playing " << _config.rate << "kHz " << (_config.channels == 2 ? "stereo" : "mono") << std::endl;
        if (_fader)
            _fader->Fade(true);

        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [&]
                 { return _reconfig.load() || !_active.load(); });

        SDL_PauseAudioDevice(device, 1);
        std::cout << _name << " Stop playing" << std::endl;
        if (_fader)
            _fader->Fade(false);
    }

    if (device)
    {
        SDL_PauseAudioDevice(device, 1);
        SDL_CloseAudioDevice(device);
    }
}
