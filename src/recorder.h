#ifndef SRC_RECORDER
#define SRC_RECORDER

#include <thread>
#include <atomic>

#include <SDL2/SDL.h>

#include "helper/isender.h"
#include "struct/audio_chunk.h"
#include "struct/atomic_queue.h"

class Recorder
{
public:
    Recorder(uint16_t buffSize);
    ~Recorder();

    void start(ISender *sender);
    void stop();

private:
    static void AudioCallback(void *userdata, Uint8 *stream, int len);
    void runner();

    ISender *_sender;
    std::atomic<bool> _active;
    std::thread _thread;
    AtomicQueue<AudioChunk> _data;
};

#endif /* SRC_RECORDER */
