#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

#include "connection.h"
#include "protocol.h" // Custom protocol header with serialize/deserialize logic

#define ID_VENDOR 0x1314
#define ID_PRODUCT 0x1520

static struct libusb_device_handle *handle = NULL;
static unsigned char ep_in, ep_out;
static pthread_mutex_t out_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t read_thread;
static int running = 1;

bool start = false;

void *read_loop(void *arg);

static DataCallback g_data_callback = nullptr;

void RegisterDataCallback(DataCallback cb) {
    g_data_callback = cb;
}


// Initialize USB connection
int usb_init_conn()
{
    std::cout << "Init connection" << std::endl;
    libusb_context *ctx = NULL;
    int r = libusb_init(&ctx);
    if (r < 0)
        return r;

    handle = libusb_open_device_with_vid_pid(ctx, ID_VENDOR, ID_PRODUCT);
    if (!handle)
        return -1;
    std::cout << "Open handle" << std::endl;

    libusb_reset_device(handle);
    std::cout << "Reset device" << std::endl;
    libusb_set_configuration(handle, 1);
    std::cout << "Set configuration" << std::endl;
    libusb_claim_interface(handle, 0);
    std::cout << "Claim interface" << std::endl;

    struct libusb_config_descriptor *config;
    libusb_get_active_config_descriptor(libusb_get_device(handle), &config);
    std::cout << "Get config descriptor" << std::endl;
    for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++)
    {
        const struct libusb_endpoint_descriptor *ep = &config->interface[0].altsetting[0].endpoint[i];
        if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
        {
            ep_in = ep->bEndpointAddress;
            std::cout << "In endpoint " << ep_in << std::endl;
        }
        else
        {
            ep_out = ep->bEndpointAddress;
            std::cout << "Out endpoint " << ep_out << std::endl;
        }
    }
    libusb_free_config_descriptor(config);
    std::cout << "Free config descriptor" << std::endl;

    pthread_create(&read_thread, NULL, read_loop, NULL);
    std::cout << "Thread started" << std::endl;
    return 0;
}

void write_uint32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
}

int usb_send_cmd(int cmd)
{
    int transferred;
    uint8_t buffer[16];

    write_uint32_le(&buffer[0], 0x55AA55AA);
    write_uint32_le(&buffer[4], 0);
    write_uint32_le(&buffer[8], cmd);
    write_uint32_le(&buffer[12], ~cmd);

    pthread_mutex_lock(&out_mutex);
    libusb_bulk_transfer(handle, ep_out, buffer, 16, &transferred, 0);
    pthread_mutex_unlock(&out_mutex);

    return transferred;
}

int usb_send_cmd_buf(int cmd, unsigned char *data, int size)
{
    int transferred;
    uint8_t buffer[16];

    write_uint32_le(&buffer[0], 0x55AA55AA);
    write_uint32_le(&buffer[4], size);
    write_uint32_le(&buffer[8], cmd);
    write_uint32_le(&buffer[12], ~cmd);

    pthread_mutex_lock(&out_mutex);
    libusb_bulk_transfer(handle, ep_out, buffer, 16, &transferred, 0);
    libusb_bulk_transfer(handle, ep_out, data, size, &transferred, 0);
    pthread_mutex_unlock(&out_mutex);

    return transferred;
}

// Send a message
int usb_send_message(struct Message *msg)
{
    int transferred;
    uint8_t buffer[1024];
    int total_len = message_serialize(msg, buffer, sizeof(buffer));

    pthread_mutex_lock(&out_mutex);
    libusb_bulk_transfer(handle, ep_out, buffer, msg->headersize, &transferred, 0);
    libusb_bulk_transfer(handle, ep_out, buffer + msg->headersize, total_len - msg->headersize, &transferred, 0);
    pthread_mutex_unlock(&out_mutex);

    return 0;
}

void sendKey(int key)
{
    printf("Send key %d", key);

    uint8_t buf[4];
    write_uint32_le(&buf[0], key);
 
    usb_send_cmd_buf(8, buf, 4);
}


void sendStart()
{
    uint8_t buf[28];
    write_uint32_le(&buf[0], 640);
    write_uint32_le(&buf[4], 480);
    write_uint32_le(&buf[8], 30);
    write_uint32_le(&buf[12], 5);
    write_uint32_le(&buf[16], 49152);
    write_uint32_le(&buf[20], 2);
    write_uint32_le(&buf[24], 2);

    usb_send_cmd_buf(1, buf, 28);
}

// Read thread
void *read_loop(void *arg)
{
    std::cout << "Loop started" << std::endl;
    while (running)
    {

        uint8_t header_buf[HEADER_SIZE];
        int transferred = 0;
        int r = libusb_bulk_transfer(handle, ep_in, header_buf, HEADER_SIZE, &transferred, 50000);

        if (r == -4)
            running = 0;

        if (r != 0)
        {
            continue;
        }

        std::cout << "Receive data status " << r << " length " << transferred << std::endl;

        if (transferred != HEADER_SIZE)
        {
            continue;
        }

        struct Message header;
        if (message_deserialize(&header, header_buf) != 0)
        {
            continue;
        }

        int data_len = header.data_len;
        uint8_t *data = NULL;
        if (data_len > 0)
        {
            data = (uint8_t *)malloc(data_len);
            if (!data)
                continue;
            libusb_bulk_transfer(handle, ep_in, data, data_len, &transferred, 1000);
        }

        struct Message *full_msg = message_upgrade(&header, data, data_len);

        if (full_msg)
        {
            std::cout << "On Message" << std::endl;

            int code = print_message_text(full_msg);

            if( code == 8 && !start)
            {
                start = true;
                sendStart();
            }

            if(code == 6)
            {
                if (g_data_callback) {
                    g_data_callback(&(full_msg->data[20]), full_msg->data_len-20);
}
            }

            
            // on_message(full_msg);
            free_message(full_msg);
        }
    }
    return NULL;
}

// Shutdown
void usb_shutdown()
{
    running = 0;
    pthread_join(read_thread, NULL);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(NULL);
}

void * connecliton_loop(void *arg)
{
    while (true)
    {
        running = 1;
        if(usb_init_conn()!=0)
        {
            sleep(5);
            continue;
        }
        while (running != 0)
        {
            usb_send_cmd(170);
            sleep(2);
        }
        usb_shutdown();
        start = false;
        sleep(5);
    }
}