#include "video_buffer.h"

extern "C"
{
#include <libavutil/imgutils.h>
}

#include <stdexcept>
#include <string>

#include "error.h"

VideoBuffer::VideoBuffer()
    : _width(0), _height(0)
{
}

// Allocate two YUV420P frames for double buffering and initialize to black
Error VideoBuffer::allocate(uint16_t width, uint16_t height)
{
    _width = width;
    _height = height;
    
    deallocate();
    reset();

    Error e;
    for (uint8_t i = 0; i < BUFFER_VIDEO_FRAMES; ++i)
    {
        // Allocate AVFrame
        _frames[i] = av_frame_alloc();
        if (e.null(_frames[i], "Failed to allocate AVFrame"))
            break;
        _frames[i]->format = AV_PIX_FMT_YUV420P;
        _frames[i]->width = width;
        _frames[i]->height = height;
        // Allocate data buffer with 32 byte allingment
        if (e.avFail(av_frame_get_buffer(_frames[i], 32), "Failed to allocate AVFrame buffer"))
            break;
        // Set Y plane to black (0)
        memset(_frames[i]->data[0], 0, _frames[i]->linesize[0] * height);
        // Set U plane to 128 (neutral)
        memset(_frames[i]->data[1], 128, _frames[i]->linesize[1] * (height / 2));
        // Set V plane to 128 (neutral)
        memset(_frames[i]->data[2], 128, _frames[i]->linesize[2] * (height / 2));
    }
    return e;
}

VideoBuffer::~VideoBuffer()
{
    deallocate();
}

void VideoBuffer::deallocate()
{
    for (uint8_t i = 0; i < BUFFER_VIDEO_FRAMES; ++i)
    {
        if (_frames[i])
        {
            // Free the frame itself
            av_frame_free(&_frames[i]);
            // Clear
            _frames[i] = nullptr;
        }
    }
}

bool VideoBuffer::getLatest(AVFrame **frame, uint32_t *id)
{
    _reading.store(_latest.load());
    int index = _reading.load();
    if (index < 0)
        return false;
    *frame = _frames[index];
    *id = _ids[index];
    return true;
}

void VideoBuffer::consumeLatest()
{
    _reading.store(-1);
}

const AVFrame *VideoBuffer::writeFrame(uint32_t id)
{
    int index = _writing.load();
    while (index == _reading.load() || index == _latest.load())
    {
        index = (index + 1) % BUFFER_VIDEO_FRAMES;
    }
    _writing.store(index);
    _ids[index] = id;
    return _frames[index];
}

void VideoBuffer::commitFrame()
{
    _latest.store(_writing.load());
}

void VideoBuffer::reset()
{
    _writing.store(0);
    _reading.store(-1);
    _latest.store(-1);
    for (uint8_t i = 0; i < BUFFER_VIDEO_FRAMES; i++)
    {
        _ids[i] = 0;
    }
}
