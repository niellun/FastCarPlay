#include "raw_queue.h"

RawQueue::RawQueue(uint16_t capacity)
    : _buffer(capacity), _head(0), _tail(0), _size(0), _capacity(capacity)
{
}

RawQueue::~RawQueue()
{
    clear();
}

bool RawQueue::push(uint8_t *data, int offset, int size)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_size == _buffer.size())
    {
        free(data);
        return false; // queue full
    }

    _buffer[_tail] = RawEntry{data, offset, size};
    _tail = (_tail + 1) % _capacity;
    _size++;

    _condition.notify_one();
    return true;
}

RawEntry RawQueue::wait(const std::atomic<bool> &reading)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _condition.wait(lock, [&]
                    { return !reading.load() || _size > 0; });

    if (!reading || _size == 0)
        return RawEntry{nullptr, 0, 0};

    RawEntry entry = _buffer[_head];
    _head = (_head + 1) % _capacity;
    _size--;
    return entry;
}

void RawQueue::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Free any remaining buffers
    while (_size > 0)
    {
        RawEntry &e = _buffer[_head];
        if (e.data)
        {
            free(e.data);
            e.data = nullptr;
        }
        _head = (_head + 1) % _capacity;
        _size--;
    }

    // Reset indices
    _head = _tail = 0;
}

void RawQueue::notify()
{
    _condition.notify_all();
}
