#include "recorder.h"

#include <iostream>
#include <cstring>

#include "helper/protocol_const.h"
#include "helper/functions.h"
#include "settings.h"


Recorder::Recorder(uint16_t buffSize)
    : _sender(nullptr), _active(false), _device(0), _data(buffSize)
{
}

Recorder::~Recorder()
{
    stop();
    if (_thread.joinable())
        _thread.join();
}

void Recorder::start(IAudioSender *sender)
{
    if (_active)
        return;

    if (_thread.joinable())
        _thread.join();

    _sender = sender;
    _active = true;
    _thread = std::thread(&Recorder::runner, this);
}

void Recorder::stop()
{
    if (!_active)
        return;
    _active = false;
    _data.notify();
}

void Recorder::AudioCallback(void *userdata, Uint8 *stream, int len)
{
    Recorder *self = static_cast<Recorder *>(userdata);
    std::unique_ptr<AudioChunk> frame(new AudioChunk(AUDIO_BUFFER_OFFSET + len));
    std::memcpy(frame.get()->data + AUDIO_BUFFER_OFFSET, stream, len);
    self->_data.pushDiscard(std::move(frame));
}

void Recorder::runner()
{
    setThreadName("recorder");

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec;

    SDL_zero(spec);
    spec.freq = 16000;
    spec.format = AUDIO_S16LSB;
    spec.channels = 1;
    spec.samples = AUDIO_BUFFER_SIZE / 2; // = 2560 bytes (1280 samples * 2 bytes)
    spec.callback = AudioCallback;
    spec.userdata = this;

    device = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &spec, nullptr, 0);
    if (device == 0)
    {
        std::cerr << "[Recording] Failed to open audio: " << SDL_GetError() << std::endl;
        _active = false;
        return;
    }

    SDL_PauseAudioDevice(device, 0);

    while (_active)
    {
        std::unique_ptr<AudioChunk> buffer = _data.pop();
        if (buffer && _sender)
            _sender->sendAudio(buffer.get()->data, buffer.get()->size);
        else if (_active)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

    }

    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
}
