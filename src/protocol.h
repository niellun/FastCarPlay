#ifndef SRC_PROTOCOL
#define SRC_PROTOCOL

#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "helper/iprotocol.h"
#include "settings.h"
#include "connector.h"

class Protocol : public IProtocol
{

public:
    Protocol(uint16_t width, uint16_t height, uint16_t fps, uint16_t padding);
    ~Protocol();

    Protocol(const Protocol &) = delete;
    Protocol &operator=(const Protocol &) = delete;

    static const char *cmdString(int cmd);

    void start(uint32_t evtStatus, uint32_t evtPhone);
    void stop();

    void sendKey(int key);
    void sendInit(int width, int height, int fps);
    void sendFile(const char *filename, const uint8_t *data, uint32_t length);
    void sendFile(const char *filename, const char *value);
    void sendFile(const char *filename, int value);
    void sendClick(float x, float y, bool down);
    void sendMove(float dx, float dy);

    Connector connector;
    AtomicQueue<Message> videoData;
    AtomicQueue<Message> audioStreamMain;
    AtomicQueue<Message> audioStreamAux;
    bool phoneConnected;

private:
    void sendInt(uint32_t cmd, uint32_t value, bool encryption = true);
    void sendEncryption();

    void onStatus(uint8_t status) override;
    void onDevice(bool connected) override;
    void onData(uint32_t cmd, uint32_t length, uint8_t *data) override;

    void onPhone(bool connected);

    uint16_t _width;
    uint16_t _height;
    uint16_t _fps;

    uint32_t _evtStatusId = (uint32_t) -1;
    uint32_t _evtPhoneId = (uint32_t) -1;
};

#endif /* SRC_PROTOCOL */
