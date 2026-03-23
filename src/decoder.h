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
#include "protocol/message.h"

template <class Buffer>
class Decoder
{

public:
    Decoder();
    ~Decoder();

    void start(AtomicQueue<Message> *data, Buffer &vb, AVCodecID codecId);
    void stop();
    void flush();

private:
    void runner();
    void loop(AVCodecContext *context, AVCodecParserContext *parser, AVPacket *packet, AVFrame *frame);
    static AVCodecContext *load_codec(AVCodecID codec_id);

    std::thread _thread;
    AVCodecContext* _context;
    AVCodecID _codecId;

    std::atomic<bool> _active = false;

    AtomicQueue<Message> *_data = nullptr;
    Buffer *_vb = nullptr;
};

extern template class Decoder<VideoBuffer>;
extern template class Decoder<VideoBufferDouble>;

#endif /* SRC_DECODER */
