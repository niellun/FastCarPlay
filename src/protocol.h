#ifndef SRC_PROTOCOL
#define SRC_PROTOCOL

#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "helper/iprotocol.h"
#include "settings.h"
#include "connector.h"

#define MAGIC 0x55aa55aa

#define CMD_OPEN 1
#define CMD_PLUGGED 2
#define CMD_UNPLUGGED 4
#define CMD_TOUCH 5
#define CMD_VIDEO_DATA 6
#define CMD_AUDIO_DATA 7
#define CMD_SEND_FILE 153

class Protocol : public IProtocol
{

public:
    Protocol(uint16_t width, uint16_t height, uint16_t fps, uint16_t padding);
    ~Protocol();

    Protocol(const Protocol &) = delete;
    Protocol &operator=(const Protocol &) = delete;

    static const char *cmdString(int cmd);

    void start(StatusCallback onStatus);
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
    AtomicQueue<Message> audioStream0;
    AtomicQueue<Message> audioStream1;
    AtomicQueue<Message> audioStream2;
    bool phoneConnected;

private:
    void onStatus(const char *status) override;
    void onDevice(bool connected) override;
    void onData(uint32_t cmd, uint32_t length, uint8_t *data) override;

    void print_message(uint32_t cmd, uint32_t length, uint8_t *data);
    void print_ints(uint32_t length, uint8_t *data, uint16_t max);
    void print_bytes(uint32_t length, uint8_t *data, uint16_t max);

    uint16_t _width;
    uint16_t _height;
    uint16_t _fps;

    StatusCallback _statusCallback = nullptr;
};

#endif /* SRC_PROTOCOL */
