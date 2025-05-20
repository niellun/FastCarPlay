#ifndef SRC_RAW_QUEUE
#define SRC_RAW_QUEUE

#include <cstdint>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>

struct RawEntry
{
    uint8_t *data;
    int offset;
    int size;
};

// Single entry: raw buffer pointer + metadata
class RawQueue
{
public:
    RawQueue(uint16_t capacity = 256);
    ~RawQueue();

    // Non-blocking push: returns false if full
    bool push(uint8_t *data, int offset, int size);

    // Blocks until an entry is available or reader_active == false
    RawEntry wait(const std::atomic<bool> &reading);

    // Clears the queue and frees any pending buffers
    void clear();

    // Unlock queus
    void notify();

private:
    std::vector<RawEntry> _buffer;
    uint16_t _head;
    uint16_t _tail;
    uint16_t _size;
    uint16_t _capacity;
    std::mutex _mutex;
    std::condition_variable _condition;
};

#endif