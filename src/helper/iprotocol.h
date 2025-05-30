#ifndef SRC_HELPER_IPROTOCOL
#define SRC_HELPER_IPROTOCOL

#include <cstdint>
#include <functional>

class IProtocol
{
public:
    virtual void onData(uint32_t cmd, uint32_t length, uint8_t *data) = 0;
    virtual void onStatus(u_int8_t status) = 0;
    virtual void onDevice(bool connected) = 0;
};
#endif /* SRC_HELPER_IPROTOCOL */
