#include "decoder.h"

#include <iostream>
#include "helper/functions.h"
#include "settings.h"

Decoder::Decoder()
    : _context(nullptr)
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

void Decoder::flush()
{
    if (_context)
        avcodec_flush_buffers(_context);
}

int Decoder::setupHardwareContext(AVCodecContext *context, const AVCodec *codec)
{
    for (int i = 0;; ++i)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config)
            return AVERROR(ENOENT);
        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            continue;

        AVBufferRef *device = nullptr;
        int ret = av_hwdevice_ctx_create(&device, config->device_type, nullptr, nullptr, 0);
        if (ret < 0)
        {
            std::cout << "[Video] Can't init HW device "
                      << av_hwdevice_get_type_name(config->device_type)
                      << " for " << codec->name << ": " << avErrorText(ret) << std::endl;
            continue;
        }

        _hwDevice = device;
        _hwPixelFormat = config->pix_fmt;
        context->opaque = this;
        context->get_format = &Decoder::getHardwareFormat;
        context->hw_device_ctx = av_buffer_ref(_hwDevice);
        std::cout << "[Video] Using HW device "
                  << av_hwdevice_get_type_name(config->device_type)
                  << " with pixel format " << av_get_pix_fmt_name(_hwPixelFormat) << std::endl;
        return 0;
    }
}

enum AVPixelFormat Decoder::getHardwareFormat(AVCodecContext *context, const enum AVPixelFormat *formats)
{
    Decoder *decoder = static_cast<Decoder *>(context->opaque);
    if (!decoder || decoder->_hwPixelFormat == AV_PIX_FMT_NONE)
        return avcodec_default_get_format(context, formats);

    for (const enum AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format)
    {
        if (*format != decoder->_hwPixelFormat)
            continue;

        AVBufferRef *frames = nullptr;
        int ret = avcodec_get_hw_frames_parameters(context, decoder->_hwDevice, decoder->_hwPixelFormat, &frames);
        if (ret == 0)
        {
            ret = av_hwframe_ctx_init(frames);
            if (ret == 0)
            {
                context->hw_frames_ctx = frames;
                return *format;
            }

            av_buffer_unref(&frames);
            std::cout << "[Video] Can't init HW frames: " << avErrorText(ret) << std::endl;
            break;
        }

        if (ret == AVERROR(ENOENT))
            return *format;

        std::cout << "[Video] Can't get HW frame parameters: " << avErrorText(ret) << std::endl;
        break;
    }

    return avcodec_default_get_format(context, formats);
}

// Initialize and select the best decoder (try HW first, then SW)
AVCodecContext *Decoder::load_codec(AVCodecID codec_id)
{
    void *iter = nullptr;
    const AVCodec *codec = nullptr;
    AVCodecContext *result = nullptr;

    while ((codec = av_codec_iterate(&iter)) && Settings::hwDecode)
    {
        if (!av_codec_is_decoder(codec) || codec->id != codec_id)
            continue;
        if (!(codec->capabilities & AV_CODEC_CAP_HARDWARE))
            continue;

        result = avcodec_alloc_context3(codec);
        if (!result)
        {
            std::cout << "[Video] Can't load HW codec " << codec->name << ": out of memory" << std::endl;
            break;
        }

        if (Settings::codecLowDelay)
            result->flags |= AV_CODEC_FLAG_LOW_DELAY;
        if (Settings::codecFast)
            result->flags2 |= AV_CODEC_FLAG2_FAST;

        int hwRet = setupHardwareContext(result, codec);
        if (hwRet < 0 && hwRet != AVERROR(ENOENT))
        {
            avcodec_free_context(&result);
            av_buffer_unref(&_hwDevice);
            _hwPixelFormat = AV_PIX_FMT_NONE;
            continue;
        }

        int ret = avcodec_open2(result, codec, nullptr);
        if (ret == 0)
        {
            std::cout << "[Video] Using HW decoder: " << codec->name;
            if (_hwPixelFormat != AV_PIX_FMT_NONE)
                std::cout << " (" << av_get_pix_fmt_name(_hwPixelFormat) << ")";
            std::cout << std::endl;
            if (result->codec->capabilities & AV_CODEC_CAP_DELAY)
                std::cout << "[Video] Codec has AV_CODEC_CAP_DELAY and can introduce lags, consider use SW decoding" << std::endl;
            return result;
        }

        std::cout << "[Video] Can't load HW decoder " << codec->name << ": " << avErrorText(ret) << std::endl;
        avcodec_free_context(&result);
        av_buffer_unref(&_hwDevice);
        _hwPixelFormat = AV_PIX_FMT_NONE;
    }

    // Fallback to software decoder
    codec = avcodec_find_decoder(codec_id);
    if (!codec)
    {
        std::cout << "[Video] HW decoder not found for codec id " << codec_id << std::endl;
        return nullptr;
    }

    result = avcodec_alloc_context3(codec);
    if (!result)
    {
        std::cout << "[Video] Failed to allocate context for codec id " << codec_id << std::endl;
        return nullptr;
    }

    if (Settings::codecLowDelay)
        result->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (Settings::codecFast)
        result->flags2 |= AV_CODEC_FLAG2_FAST;

    int ret = avcodec_open2(result, codec, nullptr);
    if (ret < 0)
    {
        std::cout << "[Video] Failed to open SW decoder " << codec->name << ": " << avErrorText(ret) << std::endl;
        avcodec_free_context(&result);
        return nullptr;
    }

    std::cout << "[Video] Using SW decoder " << codec->name << std::endl;
    return result;
}

int Decoder::transferFrame(AVCodecContext *context, AVFrame *src, AVFrame *dst)
{
    av_frame_unref(dst);
    dst->format = context->sw_pix_fmt;
    dst->width = src->width;
    dst->height = src->height;

    if (dst->format == AV_PIX_FMT_NONE)
        return AVERROR(EINVAL);

    int ret = av_frame_get_buffer(dst, 32);
    if (ret < 0)
        return ret;

    ret = av_hwframe_transfer_data(dst, src, 0);
    if (ret < 0)
        return ret;

    return av_frame_copy_props(dst, src);
}

void Decoder::runner()
{
    // Set thread name
    setThreadName("video-decoding");

    // Load codec context
    _context = load_codec(_codecId);
    if (_status.null(_context, std::string("Can't find decoder for codec ") + avcodec_get_name(_codecId)))
        return;
    std::string codec = _context->codec->name;

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
                AVFrame *transfer = av_frame_alloc();
                if (!_status.null(transfer, "Can't allocate transfer frame for codec " + codec))
                {
                    loop(_context, parser, packet, frame, transfer); // Run decoding loop
                    av_frame_free(&transfer);
                }
                av_frame_free(&frame);
            }
            av_packet_free(&packet);
        }
        av_parser_close(parser);
    }
    avcodec_free_context(&_context);
    av_buffer_unref(&_hwDevice);
    _hwPixelFormat = AV_PIX_FMT_NONE;
    _context = nullptr;

    if (_status.error())
        std::cout << "[Video] Decoder error: " << _status.message() << std::endl;
}

void Decoder::loop(AVCodecContext *context, AVCodecParserContext *parser, AVPacket *packet, AVFrame *frame, AVFrame *transfer)
{
    uint32_t counter = 0;

    // Main decoding loop; runs until global_quit flag is set
    while (_data->wait(_active))
    {
        // Get raw data segment from queue
        std::unique_ptr<Message> segment = _data->pop();
        uint8_t *data_ptr = segment->data();
        int data_size = segment->length();

        // Feed raw data into the parser and decoder
        while (_active && data_size > 0)
        {
            uint8_t *paket_data;
            int paket_size;

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
                std::cout << "[Video] Can't decode packet: " << avErrorText(send_ret) << std::endl;
                continue;
            }

            // Receive decoded frames
            while (avcodec_receive_frame(context, frame) == 0 && _active)
            {
                AVFrame *decoded = frame;
                if (_hwPixelFormat != AV_PIX_FMT_NONE && frame->format == _hwPixelFormat)
                {
                    int ret = transferFrame(context, frame, transfer);
                    if (ret < 0)
                    {
                        std::cout << "[Video] Can't transfer HW frame: " << avErrorText(ret) << std::endl;
                        av_frame_unref(frame);
                        continue;
                    }
                    av_frame_unref(frame);
                    decoded = transfer;
                }

                AVFrame *out = _vb->write(counter++);
                av_frame_unref(out);
                av_frame_move_ref(out, decoded);
                _vb->commit();
            }
        }
    }

    // push null packet to flush decoder and drain delayed frames
    if (_context)
    {
        avcodec_send_packet(context, nullptr);
        while (avcodec_receive_frame(context, frame) == 0)
        {
            AVFrame *decoded = frame;
            if (_hwPixelFormat != AV_PIX_FMT_NONE && frame->format == _hwPixelFormat)
            {
                int ret = transferFrame(context, frame, transfer);
                if (ret < 0)
                {
                    std::cout << "[Video] Can't transfer HW frame: " << avErrorText(ret) << std::endl;
                    av_frame_unref(frame);
                    continue;
                }
                av_frame_unref(frame);
                decoded = transfer;
            }

            AVFrame *out = _vb->write(counter++);
            av_frame_unref(out);
            av_frame_move_ref(out, decoded);
            _vb->commit();
        }
    }
}
