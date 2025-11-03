#ifndef SRC_NAMED_PIPE_H
#define SRC_NAMED_PIPE_H

#include <thread>
#include <atomic>
#include "protocol.h"

class PipeListener {
public:
    PipeListener(Protocol &protocol, const char *path);
    ~PipeListener();

private:
    void loop();

    Protocol &_protocol;
    const char *_path;
    bool _active;
    std::thread _thread;
};

#endif // SRC_NAMED_PIPE_H
