#ifndef SRC_VIDEO_BUFFER
#define SRC_VIDEO_BUFFER

extern "C"
{
#include <libavutil/frame.h> // For AVFrame
}

#include <atomic>

#include "helper/error.h"

#define BUFFER_VIDEO_FRAMES 3

class VideoBuffer
{
public:
    VideoBuffer();
    ~VideoBuffer();
    
    Error allocate(uint16_t width, uint16_t height);
    uint16_t width() const { return _width; };
    uint16_t height() const { return _height; };
    void reset();
    bool getLatest(AVFrame **frame, uint32_t *id);
    void consumeLatest();
    const AVFrame *writeFrame(uint32_t id);
    void commitFrame();

private:
    void deallocate();

    uint16_t _width;
    uint16_t _height;
    std::atomic<int8_t> _latest;
    std::atomic<int8_t> _reading;
    std::atomic<int8_t> _writing;
    AVFrame *_frames[BUFFER_VIDEO_FRAMES] = {nullptr, nullptr, nullptr};
    uint32_t _ids[BUFFER_VIDEO_FRAMES];
};

#endif /* SRC_VIDEO_BUFFER */
