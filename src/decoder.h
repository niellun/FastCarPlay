#ifndef SRC_DECODER
#define SRC_DECODER

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
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
    void flush();

private:
    void runner();
    void loop(AVCodecContext *context, AVCodecParserContext *parser, AVPacket *packet, AVFrame *frame, AVFrame *transfer);
    AVCodecContext *load_codec(AVCodecID codec_id);
    int setupHardwareContext(AVCodecContext *context, const AVCodec *codec);
    int transferFrame(AVCodecContext *context, AVFrame *src, AVFrame *dst);
    static enum AVPixelFormat getHardwareFormat(AVCodecContext *context, const enum AVPixelFormat *formats);

    std::thread _thread;
    AVCodecContext* _context;
    AVBufferRef *_hwDevice = nullptr;
    AVPixelFormat _hwPixelFormat = AV_PIX_FMT_NONE;
    AVCodecID _codecId;
    Error _status;

    std::atomic<bool> _active = false;

    AtomicQueue<Message> *_data = nullptr;
    VideoBuffer *_vb = nullptr;
};

#endif /* SRC_DECODER */
