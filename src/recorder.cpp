#include "recorder.h"

#include <iostream>
#include <cstring>

#include "protocol/protocol_const.h"
#include "common/functions.h"
#include "settings.h"
#include "protocol/message.h"


Recorder::Recorder()
    : _queue(nullptr), _active(false), _device(0)
{
}

Recorder::~Recorder()
{
    stop();
}

void Recorder::start(AtomicQueue<Message> *queue)
{
    if (_active)
        return;

    _queue = queue;
    _active = true;

    SDL_AudioSpec spec;

    SDL_zero(spec);
    spec.freq = 16000;
    spec.format = AUDIO_S16LSB;
    spec.channels = 1;
    spec.samples = AUDIO_BUFFER_SIZE / 2; // = 2560 bytes (1280 samples * 2 bytes)
    spec.callback = AudioCallback;
    spec.userdata = this;

    _device = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &spec, nullptr, 0);
    if (_device == 0)
    {
        std::cerr << "[Recording] Failed to open audio: " << SDL_GetError() << std::endl;
        _active = false;
        return;
    }

    SDL_PauseAudioDevice(_device, 0);
}

void Recorder::stop()
{
    if (!_active)
        return;
    _active = false;

    SDL_PauseAudioDevice(_device, 1);
    SDL_CloseAudioDevice(_device);
}

void Recorder::AudioCallback(void *userdata, Uint8 *stream, int len)
{
    Recorder *self = static_cast<Recorder *>(userdata);
    if (!self->_queue)
        return;
    std::unique_ptr<Message> message = Message::Audio(len);
    if(!message->allocated())
        return;
    std::memcpy(message->data(), stream, len);
    self->_queue->pushDiscard(std::move(message));
}
