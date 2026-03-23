#ifndef SRC_STRUCT_VIDEO_BUFFER
#define SRC_STRUCT_VIDEO_BUFFER

extern "C"
{
#include <libavutil/frame.h> // For AVFrame
}

#include <atomic>
#include <cstdint>
#include <stdexcept>

class VideoBuffer
{
public:
    VideoBuffer(int8_t size) : _reading(-1), _writing(-1), _latest(-1), _size(size), _frames(nullptr), _ids(nullptr)
    {
        if (size < 3)
            throw std::runtime_error("Minimum rendering buffer size is 3");

        _frames = new AVFrame *[_size]();
        _ids = new uint32_t[_size]();

        for (uint8_t i = 0; i < _size; ++i)
        {
            _ids[i] = 0;
            _frames[i] = av_frame_alloc();
            if (!_frames[i])
            {
                dispose();
                throw std::runtime_error("Failed to allocate AVFrame");
            }
        }
    }

    ~VideoBuffer() noexcept
    {
        dispose();
    }

    uint32_t latestId() const noexcept
    {
        const int8_t index = _latest.load(std::memory_order_acquire);
        if (index == -1)
            return 0;
        return _ids[static_cast<uint8_t>(index)];
    }

    bool consume(AVFrame **frame, uint32_t *id) noexcept
    {
        const int8_t latest = _latest.load(std::memory_order_acquire);
        int8_t index = _reading.load(std::memory_order_relaxed);
        if (index != latest)
        {
            index++;
            if (index == _size)
                index = 0;
            _reading.store(index, std::memory_order_relaxed);
        }

        if (index == -1)
            return false;

        const uint8_t slot = static_cast<uint8_t>(index);
        *frame = _frames[slot];
        *id = _ids[slot];
        return true;
    }

    AVFrame *write(uint32_t id) noexcept
    {
        int8_t index = _writing.load(std::memory_order_relaxed) + 1;
        if (index == _size)
            index = 0;
        if (index == _reading.load(std::memory_order_relaxed))
        {
            return nullptr;
        }
        _writing.store(index, std::memory_order_relaxed);

        const uint8_t slot = static_cast<uint8_t>(index);
        _ids[slot] = id;
        return _frames[slot];
    }

    void commit() noexcept
    {
        // Publish the frame contents and id written into the selected slot.
        _latest.store(_writing.load(std::memory_order_relaxed), std::memory_order_release);
    }

    void reset() noexcept
    {
        _reading.store(-1, std::memory_order_relaxed);
        _writing.store(-1, std::memory_order_relaxed);
        _latest.store(-1, std::memory_order_release);
    }

private:
    void dispose()
    {
        if (_frames)
        {
            for (uint8_t i = 0; i < _size; ++i)
            {
                if (_frames[i])
                {
                    av_frame_free(&_frames[i]);
                    _frames[i] = nullptr;
                }
            }
            delete[] _frames;
            _frames = nullptr;
        }

        if (_ids)
        {
            delete[] _ids;
            _ids = nullptr;
        }
    }

    std::atomic<int8_t> _reading;
    std::atomic<int8_t> _writing;
    std::atomic<int8_t> _latest;
    int8_t _size;
    AVFrame **_frames;
    uint32_t *_ids;
};

#endif /* SRC_STRUCT_VIDEO_BUFFER */
