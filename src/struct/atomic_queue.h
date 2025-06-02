#ifndef SRC_STRUCT_ATOMIC_QUEUE
#define SRC_STRUCT_ATOMIC_QUEUE

#include <cstdint>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>

using namespace std;

template <typename T>
class AtomicQueue
{
public:
    AtomicQueue(uint16_t size)
        : _size(size), _data(new unique_ptr<T>[size]), _first(0), _last(0), _count(0)
    {
    }

    AtomicQueue(const AtomicQueue &) = delete;
    AtomicQueue &operator=(const AtomicQueue &) = delete;

    ~AtomicQueue() = default;

    bool pushDiscard(unique_ptr<T> obj)
    {
        if (_count == _size)
            return false;

        _first = (_first + 1) % _size;
        _data[_first] = std::move(obj);
        ++_count;
        _lock.notify_one();
        return true;
    }

    bool pushReplace(unique_ptr<T> obj)
    {
        if (_count == _size)
        {
            _data[_first] = std::move(obj);
            return false;
        }

        _first = (_first + 1) % _size;
        _data[_first] = std::move(obj);
        ++_count;
        _lock.notify_one();
        return true;
    }

    const T *peek()
    {
        if (_count == 0)
            return nullptr;
        return _data[(_last + 1) % _size].get();
    }

    unique_ptr<T> pop()
    {
        if (_count == 0)
            return nullptr;

        _last = (_last + 1) % _size;
        auto item = std::move(_data[_last]);
        --_count;
        return item;
    }

    bool wait(atomic<bool> &waitFlag, uint8_t count = 0)
    {
        unique_lock<std::mutex> lock(_mtx);

        bool exit = _count > count;
        _lock.wait(lock, [&]
                   { return _count > count || !waitFlag; });
        return !exit;
    }

    void clear()
    {
        _data = std::make_unique<std::unique_ptr<T>[]>(_size);
        _first = 0;
        _last = 0;
        _count = 0;
    }

    void notify()
    {
        _lock.notify_all();
    }

    uint16_t count() { return _count; }

private:
    uint16_t _size;
    unique_ptr<unique_ptr<T>[]> _data;
    uint16_t _first;
    uint16_t _last;
    atomic<uint16_t> _count;
    mutex _mtx;
    condition_variable _lock;
};

#endif /* SRC_STRUCT_ATOMIC_QUEUE */
