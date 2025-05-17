#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC 0x55aa55aa

int message_serialize(struct Message *msg, uint8_t *buf, int maxlen)
{
    if (!msg || !buf)
        return -1;
    int total_len = HEADER_SIZE + msg->data_len;
    if (total_len > maxlen)
        return -1;

    uint32_t magic = MAGIC;
    uint32_t length = (uint32_t)msg->data_len;
    uint32_t type;
    memcpy(&type, msg->header + 8, sizeof(type));
    uint32_t typecheck = ~type;

    memcpy(buf, &magic, sizeof(magic));
    memcpy(buf + 4, &length, sizeof(length));
    memcpy(buf + 8, &type, sizeof(type));
    memcpy(buf + 12, &typecheck, sizeof(typecheck));

    if (msg->data_len > 0 && msg->data)
    {
        memcpy(buf + HEADER_SIZE, msg->data, msg->data_len);
    }
    return total_len;
}

int message_deserialize(struct Message *msg, uint8_t *buf)
{
    if (!msg || !buf)
        return -1;

    memcpy(msg->header, buf, HEADER_SIZE);
    msg->headersize = HEADER_SIZE;

    uint32_t length;
    memcpy(&length, buf + 4, sizeof(length));
    msg->data_len = (int)length;

    if (msg->data_len > 0)
    {
        msg->data = (uint8_t *)malloc(msg->data_len);
        if (!msg->data)
            return -1;
        // memcpy(msg->data, buf + HEADER_SIZE, msg->data_len);
    }
    else
    {
        msg->data = NULL;
    }
    return 0;
}

struct Message *message_upgrade(struct Message *msg, uint8_t *data, int len)
{
    memcpy(msg->data, data, len);
    msg->data_len = len;
    return msg;
}

void free_message(struct Message *msg)
{
    if (!msg)
        return;
    if (msg->data)
    {
        free(msg->data);
        msg->data = NULL;
    }
}

int print_message_text(const struct Message *msg)
{
    if (!msg)
        return -1;

    uint32_t magic, length, type, typecheck;
    memcpy(&magic, msg->header + 0, sizeof(magic));
    memcpy(&length, msg->header + 4, sizeof(length));
    memcpy(&type, msg->header + 8, sizeof(type));
    memcpy(&typecheck, msg->header + 12, sizeof(typecheck));

    printf("Message: %-10u Size: %-6u Magic: %-5s Check: %-5s\n",
           type,
           (unsigned int)length,
           (magic == 0x55AA55AA ? "good" : "fail"),
           (typecheck == ~type ? "good" : "fail"));

    // Line 2: Payload as list of integers
    if (msg->data_len >= 4 && msg->data)
    {
        printf("Payload: ");
        size_t count = msg->data_len / 4;
        for (size_t i = 0; (i < count) & (i < 8); ++i)
        {
            uint32_t val =
                ((uint32_t)msg->data[i * 4 + 0]) |
                ((uint32_t)msg->data[i * 4 + 1] << 8) |
                ((uint32_t)msg->data[i * 4 + 2] << 16) |
                ((uint32_t)msg->data[i * 4 + 3] << 24);
            printf("%u", val);
            if (i != count - 1)
                printf(", ");
        }
        printf("\n");
    }
    else
    {
        printf("Payload: <none>\n");
    }

    return type;
}
