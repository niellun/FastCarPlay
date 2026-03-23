#include "protocol/connection_reader.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "settings.h"
#include "common/logger.h"
#include "common/threading.h"
#include "protocol/protocol_const.h"
#include "libavcodec/defs.h"

ConnectionReader::ConnectionReader()
    : _active(false),
      _buffer(Settings::usbBuffer, Settings::usbTransferSize),
      _transfers(Settings::usbQueue),
      _receiver(nullptr),
      _usbContext(nullptr)
{
    log_v("Created");
}

ConnectionReader::~ConnectionReader()
{
    log_v("Destroying");

    stop();

    for (Context &context : _transfers)
    {
        if (context.transfer)
        {
            libusb_free_transfer(context.transfer);
            context.transfer = nullptr;
        }
    }
    log_v("Destroyed");
}

bool ConnectionReader::start(libusb_context *context, libusb_device_handle *device, uint8_t endpoint, IMessageReceiver *receiver)
{
    if (_active || !context || !device)
        return false;

    _receiver = receiver;
    _usbContext = context;

    log_i("Starting to read endpoint %d with %d requests", endpoint, _transfers.size());

    // Prepare usb transfers
    for (Context &context : _transfers)
    {
        context.owner = this;
        if (!context.transfer)
        {
            context.transfer = libusb_alloc_transfer(0);
            if (context.transfer == nullptr)
            {
                log_e("Can't allocate usb transfer");
                return false;
            }
        }
    }

    // Start processing thread
    _buffer.reset();
    _active = true;
    _processThread = std::thread(&ConnectionReader::processLoop, this);
    _readThread = std::thread(&ConnectionReader::readLoop, this);

    // Start usb reading thread
    for (Context &context : _transfers)
    {
        context.slot = _buffer.get();
        if (context.slot == nullptr)
        {
            log_e("Can't allocate data slot for usb transfer");
            return false;
        }
        context.owner = this;
        libusb_fill_bulk_transfer(context.transfer, device, endpoint, context.slot->data, context.slot->size, ConnectionReader::onUsbRead, &context, 0);
        int status = libusb_submit_transfer(context.transfer);
        if (status != LIBUSB_SUCCESS)
        {
            log_w("USB transfer submit failed with code %d", status);
            return false;
        }
    }

    return true;
}

void ConnectionReader::stop()
{
    log_v("Stopping");

    _active = false;

    if (_usbContext)
    {
        for (Context &context : _transfers)
        {
            if (context.transfer)
                libusb_cancel_transfer(context.transfer);
        }

        timeval timeout{0, 100000};
        libusb_handle_events_timeout_completed(_usbContext, &timeout, nullptr);

        log_v("Events canceled");
    }

    _buffer.notify();

    if (_readThread.joinable())
        _readThread.join();

    if (_processThread.joinable())
        _processThread.join();

    _usbContext = nullptr;

    log_v("Threads stopped");
}

void ConnectionReader::onUsbRead(libusb_transfer *transfer)
{
    if (!transfer || !transfer->user_data)
        return;

    Context *c = static_cast<Context *>(transfer->user_data);
    if (!c->owner->_active)
        return;

    log_p("USB read > status %d length %d", transfer->status, transfer->actual_length);

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
        return;

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
        log_w("USB read failed with status %d", transfer->status);
        c->owner->_active = false;
        return;
    }

    c->slot->commit(transfer->actual_length);

    c->slot = c->owner->_buffer.get();
    if (!c->slot)
    {
        log_e("Can't allocate data slot for next usb transfer");
        c->owner->_active = false;
        return;
    }
    c->transfer->buffer = c->slot->data;

    if (!c->owner->_active)
        return;

    int status = libusb_submit_transfer(c->transfer);
    if (status != LIBUSB_SUCCESS)
    {
        log_w("USB transfer re-submit failed with status %d", status);
        c->owner->_active = false;
    }
}

void ConnectionReader::readLoop()
{
    setThreadName("usb-read");
    timeval timeout{0, 100000};

    log_d("USB reading thread started");

    while (_active)
    {
        libusb_handle_events_timeout_completed(_usbContext, &timeout, nullptr);
    }

    log_v("USB reading thread stopped");
}

void ConnectionReader::processLoop()
{
    setThreadName("usb-process");
    log_d("USB processing thread started");

    while (_active)
    {
        std::unique_ptr<Message> message = std::make_unique<Message>();

        if (!_buffer.read(message->header(), message->headerSize(), _active))
            break;

        if (!message->valid())
        {
            log_w("Received mallformed message");
            continue;
        }

        if (message->length() >= 0)
        {
            uint8_t *buff = message->allocate(message->type() == CMD_VIDEO_DATA ? AV_INPUT_BUFFER_PADDING_SIZE : 0);
            if (!_buffer.read(buff, message->length(), _active))
                break;
        }

        if (_receiver && message->allocated())
            _receiver->onMessage(std::move(message));
    }

    log_v("USB processing thread stopped");
}
