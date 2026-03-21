#ifndef SRC_PROTOCOL
#define SRC_PROTOCOL

#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "connector.h"
#include "recorder.h"

class Protocol : public Connector
{

public:
    Protocol(uint16_t width, uint16_t height, uint16_t fps);
    ~Protocol() override;

    Protocol(const Protocol &) = delete;
    Protocol &operator=(const Protocol &) = delete;

    void start(uint32_t evtStatus, uint32_t evtPhone);
    void stop();

    AtomicQueue<Message> videoData;
    AtomicQueue<Message> audioStreamMain;
    AtomicQueue<Message> audioStreamAux;

private:
    void sendConfig();

    void onStatus(uint8_t status) override;
    void onDevice(bool connected) override;
    void onData(uint8_t *data, uint32_t length) override;

    void dispatch(std::unique_ptr<Message> msg);
    void onControl(int cmd);
    void onPhone(bool connected);

    Recorder _recorder;
    uint16_t _width;
    uint16_t _height;
    uint16_t _fps;
    bool _phoneConnected;

    uint32_t _evtStatusId = (uint32_t)-1;
    uint32_t _evtPhoneId = (uint32_t)-1;

    std::unique_ptr<Message> _message;
};

#endif /* SRC_PROTOCOL */
