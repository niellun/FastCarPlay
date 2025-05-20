#ifndef SRC_DECODER
#define SRC_DECODER

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <thread>

#include "struct/video_buffer.h"
#include "struct/atomic_queue.h"
#include "struct/message.h"
#include "helper/error.h"

class Decoder
{

public:
    Decoder();
    ~Decoder();

    void start(AtomicQueue<Message> *data, VideoBuffer *vb,  AVCodecID codecId);
    void stop();

private:
    void runner();
    void loop(AVCodecContext *context, AVCodecParserContext *parser, AVPacket *packet, AVFrame *frame);
    static AVCodecContext *load_codec(AVCodecID codec_id);

    std::thread _thread;
    AVCodecID _codecId;
    Error _status;

    std::atomic<bool> _active = false;

    AtomicQueue<Message> *_data = nullptr;
    VideoBuffer *_vb = nullptr;
};

#endif /* SRC_DECODER */
