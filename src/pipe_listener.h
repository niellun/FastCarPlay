#ifndef SRC_PIPE_LISTENER
#define SRC_PIPE_LISTENER

#include <thread>
#include <atomic>
#include "protocol.h"

class PipeListener {
public:
    PipeListener(const char *path);
    ~PipeListener();

private:
    void loop();

    const char *_path;
    bool _active;
    std::thread _thread;
};

#endif /* SRC_PIPE_LISTENER */
