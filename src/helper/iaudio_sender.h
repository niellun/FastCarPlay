#ifndef SRC_HELPER_IAUDIO_SENDER
#define SRC_HELPER_IAUDIO_SENDER

#include <cstdint>

class IAudioSender {
public:
    virtual ~IAudioSender() = default; 
    virtual void sendAudio(uint8_t* data, uint32_t length) = 0;
};

#endif /* SRC_HELPER_IAUDIO_SENDER */
