#include "decoder.h"

#include <iostream>
#include "helper/functions.h"
#include "settings.h"

Decoder::Decoder()
{
}

Decoder::~Decoder()
{
    stop();
}

void Decoder::start(AtomicQueue<Message> *data, VideoBuffer *vb, AVCodecID codecId)
{
    if (_active)
        stop();

    _vb = vb;
    _data = data;
    _codecId = codecId;
    _active = true;
    _thread = std::thread(&Decoder::runner, this);
}
void Decoder::stop()
{
    if (!_active)
        return;
    _active = false;
    _data->notify();
    if (_thread.joinable())
        _thread.join();
}

// Initialize and select the best decoder (try HW first, then SW)
AVCodecContext *Decoder::load_codec(AVCodecID codec_id)
{
    void *iter = nullptr;
    const AVCodec *codec = nullptr;
    AVCodecContext *result = nullptr;

    // Try hardware-accelerated decoders by iterating registered codecs
    while ((codec = av_codec_iterate(&iter)))
    {
        if (!av_codec_is_decoder(codec) || codec->id != codec_id)
            continue;
        if (!(codec->capabilities & AV_CODEC_CAP_HARDWARE))
            continue;

        result = avcodec_alloc_context3(codec);
        if (!result)
        {
            std::cout << "Can't load HW codec " << codec->name << ": out of memory" << std::endl;
            break;
        }

        int ret = avcodec_open2(result, codec, nullptr);
        if (ret == 0)
        {
            std::cout << "Using HW decoder: " << codec->name << std::endl;
            return result;
        }

        std::cout << "Can't load HW codec " << codec->name << ": " << Error::avErrorText(ret) << std::endl;
        avcodec_free_context(&result);
    }

    // Fallback to software decoder
    codec = avcodec_find_decoder(codec_id);
    if (!codec)
    {
        std::cout << "Decoder not found for codec id " << codec_id << std::endl;
        return nullptr;
    }

    result = avcodec_alloc_context3(codec);
    if (!result)
    {
        std::cout << "Failed to allocate context for codec id " << codec_id << std::endl;
        return nullptr;
    }

    int ret = avcodec_open2(result, codec, nullptr);
    if (ret < 0)
    {
        std::cout << "Failed to open software decoder " << codec->name << ": " << Error::avErrorText(ret) << std::endl;
        avcodec_free_context(&result);
        return nullptr;
    }

    std::cout << "Using SW decoder " << codec->name << std::endl;
    return result;
}

void Decoder::runner()
{
    // Set thread name
    setThreadName( "video-decoding");

    // Load codec context
    AVCodecContext *context = load_codec(_codecId);
    if (_status.null(context, ("Can't find decoder for codec " + _codecId)))
        return;
    std::string codec = context->codec->name;

    // Initialize parser for the codec
    AVCodecParserContext *parser = av_parser_init(_codecId);
    if (!_status.null(parser, "Can't initilise parser for codec " + codec))
    {
        // Allocate packet for decoding
        AVPacket *packet = av_packet_alloc();
        if (!_status.null(packet, "Can't allocate packet for codec " + codec))
        {
            // Allocate frame for decoded data
            AVFrame *frame = av_frame_alloc();
            if (!_status.null(frame, "Can't allocate frame for codec " + codec))
            {
                loop(context, parser, packet, frame); // Run decoding loop
                av_frame_free(&frame);
            }
            av_packet_free(&packet);
        }
        av_parser_close(parser);
    }
    avcodec_free_context(&context);

    if (_status.error())
        std::cout << "Video decoder error: " << _status.message() << std::endl;
}

void Decoder::loop(AVCodecContext *context, AVCodecParserContext *parser, AVPacket *packet, AVFrame *frame)
{
    uint32_t counter = 0;

    std::cout << "Video decoder loop started" << std::endl;

    // Main decoding loop; runs until global_quit flag is set
    while (_active)
    {
        // Get raw data segment from queue
        std::unique_ptr<Message> segment = _data->wait(_active);

        if (!_active)
            continue;

        uint8_t *data_ptr = segment->data();
        int data_size = segment->length();

        // Feed raw data into the parser and decoder
        while (_active && data_size > 0)
        {
            uint8_t *paket_data;
            int paket_size;

            // printf("avparser offset %d size %d fullsize %d\n", data_ptr-segment.data, data_size, segment.size);

            // Parse raw data into packets
            int len = av_parser_parse2(parser, context,
                                       &paket_data, &paket_size,
                                       data_ptr, data_size,
                                       AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

            // Parsing error; break out
            if (len < 0)
                break;

            // Move forward through segment
            data_ptr += len;
            data_size -= len;

            if (paket_size <= 0)
                continue;

            // Load packet data
            av_packet_unref(packet);
            packet->data = paket_data;
            packet->size = paket_size;

            // Send packet to decoder
            int send_ret = avcodec_send_packet(context, packet);
            if (send_ret != 0)
            {
                std::cout << "Error decoding packet: " << Error::avErrorText(send_ret) << std::endl;
                continue;
            }

            // Receive decoded frames
            while (avcodec_receive_frame(context, frame) == 0 && _active)
            {
                AVFrame* out = _vb->write(counter++);
                av_frame_unref(out);
                av_frame_move_ref(out, frame);
                _vb->commit();
            }
        }
    }
}
