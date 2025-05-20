#ifndef SRC_STRUCT_VIDEO_BUFFER
#define SRC_STRUCT_VIDEO_BUFFER

extern "C"
{
#include <libavutil/frame.h> // For AVFrame
}

#include <atomic>
#include <stdexcept>

#define BUFFER_VIDEO_FRAMES 3

class VideoBuffer
{
public:
    VideoBuffer()
    {
        _writing.store(0);
        _reading.store(-1);
        _latest.store(-1);
        for (uint8_t i = 0; i < BUFFER_VIDEO_FRAMES; ++i)
        {
            _ids[i] = 0;
            _frames[i] = av_frame_alloc();
            if (!_frames[i])
            {
                throw std::runtime_error("Failed to allocate AVFrame");
            }
        }
    }

    ~VideoBuffer()
    {
        for (uint8_t i = 0; i < BUFFER_VIDEO_FRAMES; ++i)
        {
            if (_frames[i])
            {
                av_frame_free(&_frames[i]);
                _frames[i] = nullptr;
            }
        }
    }

    bool latest(AVFrame **frame, uint32_t *id)
    {
        _reading.store(_latest.load());
        int index = _reading.load();
        if (index < 0)
            return false;
        *frame = _frames[index];
        *id = _ids[index];
        return true;
    }

    void consume()
    {
        _reading.store(-1);
    }

    AVFrame *write(uint32_t id)
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

    void commit()
    {
        _latest.store(_writing.load());
    }

private:
    std::atomic<int8_t> _latest;
    std::atomic<int8_t> _reading;
    std::atomic<int8_t> _writing;
    AVFrame *_frames[BUFFER_VIDEO_FRAMES] = {nullptr, nullptr, nullptr};
    uint32_t _ids[BUFFER_VIDEO_FRAMES];
};

#endif /* SRC_STRUCT_VIDEO_BUFFER */
