#ifndef SRC_STRUCT_USB_BUFFER
#define SRC_STRUCT_USB_BUFFER

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <cstring>

class DataSlot
{
public:
    DataSlot()
        : ready(false), offset(0), length(0), size(0), data(nullptr), _cv(nullptr)
    {
    }

    ~DataSlot()
    {
        size = 0;
        if (data)
        {
            free(data);
            data = nullptr;
        }
    }

    void init(uint32_t slotSize, std::condition_variable *condition)
    {
        ready = false;
        offset = 0;
        length = 0;
        size = slotSize;
        data = static_cast<uint8_t *>(malloc(size));
        _cv = condition;
    }

    void reset()
    {
        ready = false;
        offset = 0;
        length = 0;
    }

    void commit(size_t dataSize)
    {
        length = dataSize;
        offset = 0;
        ready = true;

        if (_cv)
            _cv->notify_one();
    }

    bool consume(size_t dataSize)
    {
        offset += dataSize;
        if (offset < length)
            return false;
        ready = false;
        return true;
    }

    size_t remain() const { return length > offset ? length - offset : 0; }

    bool ready;
    size_t offset;
    size_t length;
    size_t size;
    uint8_t *data;

private:
    std::condition_variable *_cv;
};

class UsbBuffer
{
public:
    UsbBuffer(uint16_t slotCount, size_t slotSize)
        : _active(true), _size(slotCount)
    {
        if (slotCount == 0 || slotSize == 0)
            throw std::invalid_argument("[UsbBuffer] Number of slots and slot size must be greater than 0");

        _slots = new DataSlot[_size];

        for (uint16_t i = 0; i < _size; i++)
        {
            _slots[i].init(slotSize, &_cvReady);
        }
    }

    UsbBuffer(const UsbBuffer &) = delete;
    UsbBuffer &operator=(const UsbBuffer &) = delete;

    ~UsbBuffer()
    {
        stop();
        if (_slots)
        {
            delete[] _slots;
        }
    }

    void start()
    {
        _readSlot = 0;
        _writeSlot = 0;
        for (uint16_t i = 0; i < _size; i++)
        {
            _slots[i].reset();
        }
        _active = true;
    }

    void stop()
    {
        _active = false;
        std::lock_guard<std::mutex> lock(_mutex);
        _cvReady.notify_all();
    }

    DataSlot *get()
    {
        if (!_active || _slots[_writeSlot].ready)
            throw std::runtime_error("[UsbBuffer] No free slots available");
        DataSlot *slot = &(_slots[_writeSlot]);
        _writeSlot++;
        if (_writeSlot >= _size)
            _writeSlot = 0;
        return slot;
    }

    bool read(uint8_t *dst, size_t length)
    {
        if (length == 0)
            return true;

        if (dst == nullptr)
            throw std::invalid_argument("[UsbBuffer] Read destination is null");

        size_t done = 0;
        while (length > 0)
        {
            if (!_active)
                return false;

            while (!_slots[_readSlot].ready)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cvReady.wait(lock, [&]()
                              { return !_active || _slots[_readSlot].ready; });
                if (!_active)
                    return false;
            }

            size_t copy = _slots[_readSlot].remain();
            if (copy > length)
                copy = length;
            std::memcpy(dst + done, _slots[_readSlot].data + _slots[_readSlot].offset, copy);
            if (_slots[_readSlot].consume(copy))
            {
                _readSlot++;
                if (_readSlot >= _size)
                    _readSlot = 0;
            }
            done += copy;
            length -= copy;
        }

        return true;
    }

    int count() const {
        int result = _writeSlot - _readSlot;
        if(result<0)
            result += _size;
        return result;
    }

private:
    mutable std::mutex _mutex;
    std::condition_variable _cvReady;

    std::atomic<bool> _active;

    uint16_t _writeSlot = 0;
    uint16_t _readSlot = 0;

    DataSlot *_slots = nullptr;
    uint16_t _size = 0;
};

#endif /* SRC_STRUCT_USB_BUFFER */
