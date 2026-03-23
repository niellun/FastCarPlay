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
    VideoBuffer()
    {
        _writing = 0;
        _reading.store(-1);
        _latest.store(-1);
        for (uint8_t i = 0; i < 3; ++i)
        {
            _ids[i] = 0;
            _frames[i] = av_frame_alloc();
            if (!_frames[i])
            {
                throw std::runtime_error("Failed to allocate AVFrame");
            }
        }
    }

    ~VideoBuffer() noexcept
    {
        for (uint8_t i = 0; i < 3; ++i)
        {
            if (_frames[i])
            {
                av_frame_free(&_frames[i]);
                _frames[i] = nullptr;
            }
        }
    }

    uint32_t latestId() const noexcept
    {
        const int8_t index = _latest.load(std::memory_order_acquire);
        if (index == -1)
            return 0;
        return _ids[static_cast<uint8_t>(index)];
    }

    bool latest(AVFrame **frame, uint32_t *id) noexcept
    {
        const int8_t index = _latest.load(std::memory_order_acquire);
        _reading.store(index, std::memory_order_seq_cst);
        if (index == -1)
            return false;
        const uint8_t slot = static_cast<uint8_t>(index);
        *frame = _frames[slot];
        *id = _ids[slot];
        return true;
    }

    void consume() noexcept
    {
        _reading.store(-1, std::memory_order_seq_cst);
    }

    AVFrame *write(uint32_t id) noexcept
    {
        int8_t index = _writing;
        while (index == _reading.load(std::memory_order_seq_cst) ||
               index == _latest.load(std::memory_order_relaxed))
        {
            ++index;
            if (index == 3)
                index = 0;
        }
        _writing = index;
        const uint8_t slot = static_cast<uint8_t>(index);
        _ids[slot] = id;
        return _frames[slot];
    }

    void commit() noexcept
    {
        _latest.store(_writing, std::memory_order_release);
    }

    void reset() noexcept
    {
        _reading.store(-1);
        _latest.store(-1);
    }

private:
    std::atomic<int8_t> _latest;
    std::atomic<int8_t> _reading;
    int8_t _writing;
    AVFrame *_frames[3];
    uint32_t _ids[3];
};

class VideoBufferDouble
{
public:
    VideoBufferDouble()
    {
        _writing = 0;
        _oldest.store(-1);
        _reading.store(-1);
        _latest.store(-1);
        for (uint8_t i = 0; i < 4; ++i)
        {
            _ids[i] = 0;
            _frames[i] = av_frame_alloc();
            if (!_frames[i])
            {
                throw std::runtime_error("Failed to allocate AVFrame");
            }
        }
    }

    ~VideoBufferDouble() noexcept
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (_frames[i])
            {
                av_frame_free(&_frames[i]);
                _frames[i] = nullptr;
            }
        }
    }

    uint32_t latestId() const noexcept
    {
        const int8_t index = _latest.load(std::memory_order_acquire);
        if (index == -1)
            return 0;
        return _ids[static_cast<uint8_t>(index)];
    }

    bool latest(AVFrame **frame, uint32_t *id) noexcept
    {
        int8_t index = _oldest.load(std::memory_order_acquire);
        _reading.store(index, std::memory_order_seq_cst);
        if (index == -1)
        {
            index = _latest.load(std::memory_order_acquire);
            _reading.store(index, std::memory_order_seq_cst);
            if (index == -1)
                return false;
        }
        const uint8_t slot = static_cast<uint8_t>(index);
        *frame = _frames[slot];
        *id = _ids[slot];
        return true;
    }

    void consume() noexcept
    {
        const int8_t reading = _reading.load(std::memory_order_seq_cst);
        if (_oldest.load(std::memory_order_relaxed) == reading)
            _oldest.store(-1, std::memory_order_relaxed);
        _reading.store(-1, std::memory_order_seq_cst);
    }

    AVFrame *write(uint32_t id) noexcept
    {
        int8_t index = _writing;
        while (index == _reading.load(std::memory_order_seq_cst) ||
               index == _latest.load(std::memory_order_relaxed) ||
               index == _oldest.load(std::memory_order_relaxed))
        {
            ++index;
            if (index == 4)
                index = 0;
        }
        _writing = index;
        const uint8_t slot = static_cast<uint8_t>(index);
        _ids[slot] = id;
        return _frames[slot];
    }

    void commit() noexcept
    {
        _oldest.store(_latest.load(std::memory_order_relaxed), std::memory_order_release);
        _latest.store(_writing, std::memory_order_release);
    }

    void reset() noexcept
    {
        _oldest.store(-1);
        _reading.store(-1);
        _latest.store(-1);
    }

private:
    std::atomic<int8_t> _oldest;
    std::atomic<int8_t> _latest;
    std::atomic<int8_t> _reading;
    int8_t _writing;
    AVFrame *_frames[4];
    uint32_t _ids[4];
};

#endif /* SRC_STRUCT_VIDEO_BUFFER */
