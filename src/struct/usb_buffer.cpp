#include "struct/usb_buffer.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

DataSlot::DataSlot()
    : ready(false), offset(0), length(0), size(0), data(nullptr), _cv(nullptr)
{
}

DataSlot::~DataSlot()
{
    size = 0;
    if (data)
    {
        free(data);
        data = nullptr;
    }
}

void DataSlot::init(uint32_t slotSize, std::condition_variable *condition)
{
    ready.store(false);
    offset = 0;
    length = 0;
    size = slotSize;
    data = static_cast<uint8_t *>(malloc(size));
    _cv = condition;
}

void DataSlot::reset()
{
    ready.store(false);
    offset = 0;
    length = 0;
}

void DataSlot::commit(size_t dataSize)
{
    length = dataSize;
    offset = 0;
    ready.store(true);

    if (_cv)
        _cv->notify_one();
}

bool DataSlot::consume(size_t dataSize)
{
    offset += dataSize;
    if (offset < length)
        return false;
    ready.store(false);
    return true;
}

size_t DataSlot::remain() const
{
    return length > offset ? length - offset : 0;
}

UsbBuffer::UsbBuffer(uint16_t slotCount, uint32_t slotSize)
    : _slots(nullptr), _size(slotCount), _writeSlot(0), _readSlot(0)
{
    if (slotCount == 0 || slotSize == 0)
        throw std::invalid_argument("Number of slots and slot size must be greater than 0");

    _slots = new DataSlot[_size];

    for (uint16_t i = 0; i < _size; i++)
    {
        _slots[i].init(slotSize, &_cv);
    }
}

UsbBuffer::~UsbBuffer()
{
    _cv.notify_all();
    if (_slots)
    {
        delete[] _slots;
    }
}

DataSlot *UsbBuffer::get()
{
    if (_slots[_writeSlot].ready.load())
        return nullptr;
    DataSlot *slot = &(_slots[_writeSlot]);
    _writeSlot++;
    if (_writeSlot >= _size)
        _writeSlot = 0;
    return slot;
}

bool UsbBuffer::read(uint8_t *dst, uint32_t length, std::atomic<bool> &active)
{
    if (length == 0)
        return true;

    size_t done = 0;
    while (length > 0)
    {
        while (!_slots[_readSlot].ready.load())
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [&]()
                     { return !active.load() || _slots[_readSlot].ready.load(); });
            if (!active.load())
                return false;
        }

        size_t copy = _slots[_readSlot].remain();
        if (copy > length)
            copy = length;
        if (dst != nullptr)
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

    return active.load();
}

void UsbBuffer::reset()
{
    _readSlot = 0;
    _writeSlot = 0;
    for (uint16_t i = 0; i < _size; i++)
    {
        _slots[i].reset();
    }
}

void UsbBuffer::notify()
{
    _cv.notify_all();
}

int UsbBuffer::count() const
{
    int result = _writeSlot - _readSlot;
    if (result < 0)
        result += _size;
    return result;
}
