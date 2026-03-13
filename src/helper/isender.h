#ifndef SRC_HELPER_ISENDER
#define SRC_HELPER_ISENDER

#include <memory>

#include "struct/command.h"

class ISender
{
public:
    virtual ~ISender() = default;
    virtual bool send(std::unique_ptr<Command> packet) = 0;
};

#endif /* SRC_HELPER_ISENDER */
