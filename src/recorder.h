#ifndef SRC_RECORDER
#define SRC_RECORDER

#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "struct/atomic_queue.h"
#include "protocol/message.h"

class Recorder
{
public:
    Recorder();
    ~Recorder();

    void start(AtomicQueue<Message> *queue);
    void stop();

private:
    static void AudioCallback(void *userdata, Uint8 *stream, int len);
    void runner();

    AtomicQueue<Message> *_queue;
    std::atomic<bool> _active;
    SDL_AudioDeviceID _device;        
};

#endif /* SRC_RECORDER */
