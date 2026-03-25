#ifndef SRC_STRUCT_USB_BUFFER
#define SRC_STRUCT_USB_BUFFER

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>

class DataSlot
{
public:
    DataSlot();
    ~DataSlot();

    void init(uint32_t slotSize, std::condition_variable *condition);
    void reset();
    void commit(size_t dataSize);
    bool consume(size_t dataSize);
    size_t remain() const;

    std::atomic<bool> ready;
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
    UsbBuffer(uint16_t slotCount, uint32_t slotSize);
    ~UsbBuffer();

    UsbBuffer(const UsbBuffer &) = delete;
    UsbBuffer &operator=(const UsbBuffer &) = delete;

    DataSlot *get();
    bool read(uint8_t *dst, uint32_t length, std::atomic<bool> &active);
    void discard();

    void reset();
    void notify();
    int count() const;

private:
    std::mutex _mutex;
    std::condition_variable _cv;

    DataSlot *_slots;
    uint16_t _size;
    uint16_t _writeSlot;
    uint16_t _readSlot;
};

#endif /* SRC_STRUCT_USB_BUFFER */
