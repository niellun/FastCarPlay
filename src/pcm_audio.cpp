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
PcmAudio::PcmAudio() {}

PcmAudio::~PcmAudio()
{
    stop();
}

void PcmAudio::callback(void *userdata, Uint8 *stream, int len)
{
    PcmAudio *self = static_cast<PcmAudio *>(userdata);
    const Message *segment = self->_data->peek();
    if (segment && self->_offset == 0 && getConfig(segment->getInt(OFFSET_AUDIO_FORMAT)) != self->_config)
    {
        self->_reconfig = true;
        self->_cv.notify_one();
    }
    while (len > 0)
    {
        if (segment == nullptr || self->_reconfig)
        {
            std::fill_n(stream, len, 0);
            if (self->_nodata++ > NO_DATA_FRAMES)
            {
                self->_reconfig = true;
                self->_cv.notify_one();
            }
            return;
        }

        self->_nodata = 0;
        int remain = segment->length() - self->_offset;
        uint8_t *data = segment->data() + self->_offset;

        if (remain > len)
        {
            std::memcpy(stream, data, len);
            self->_offset = self->_offset + len;
            return;
        }

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
    _cv.notify_all();
}

void PcmAudio::runner()
{
    setThreadName("audio");

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec;

    while (_active)
    {
        printf("AUDIO - WAIT BUFFER\n");
        _data->wait(_active, Settings::audioDelay);
        const Message *segment = _data->peek();
        if (!segment)
            continue;

        printf("AUDIO - START\n");
        _config = getConfig(segment->getInt(OFFSET_AUDIO_FORMAT));
        if (device != 0)
        {
            SDL_PauseAudioDevice(device, 1);
            SDL_CloseAudioDevice(device);
            device = 0;
        }

        // Configure new spec
        SDL_zero(spec);
        spec.freq = _config.rate;
        spec.format = AUDIO_S16SYS;
        spec.channels = _config.channels;
        spec.samples = 4096;
        spec.callback = callback;
        spec.userdata = this;

        _reconfig = false;
        _offset = 0;
        _nodata = 0;

        device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        if (device == 0)
        {
            std::cerr << "[Audio] Failed to open audio: " << SDL_GetError() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        SDL_PauseAudioDevice(device, 0);

        printf("AUDIO - SLEED %b\n", _reconfig.load() || !_active.load());
        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [&]
                 { return _reconfig.load() || !_active.load(); });
    }

    if (device)
    {
        SDL_PauseAudioDevice(device, 1);
        SDL_CloseAudioDevice(device);
    }
}
