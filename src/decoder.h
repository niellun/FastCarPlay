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
#include "struct/raw_queue.h"

class Decoder
{

public:
    Decoder();
    ~Decoder();

    void start(RawQueue *data, VideoBuffer *vb,  AVCodecID codecId);
    void stop();

private:
    void runner();
    void loop(AVCodecContext *context, AVCodecParserContext *parser, AVPacket *packet, AVFrame *frame);
    static AVCodecContext *load_codec(AVCodecID codec_id);

    std::thread _thread;
    AVCodecID _codecId;
    Error _status;

    std::atomic<bool> _active = false;
    std::atomic<bool> _running = false;

    RawQueue *_data = nullptr;
    VideoBuffer *_vb = nullptr;
};

#endif /* SRC_DECODER */
