#ifndef SRC_HELPER_ISENDER
#define SRC_HELPER_ISENDER

#include <memory>

#include "protocol/message.h"

class IMessageSender
{
public:
    virtual ~IMessageSender() = default;
    virtual bool send(std::unique_ptr<Message> packet) = 0;
};

#endif /* SRC_HELPER_ISENDER */
