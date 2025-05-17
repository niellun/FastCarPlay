#ifndef SRC_PROTOCOL
#define SRC_PROTOCOL

#include <stdint.h>

// 4 fields × 4 bytes each: magic, length, type, typecheck
#define HEADER_SIZE 16

struct Message {
    uint8_t  header[HEADER_SIZE];
    int      headersize;
    int      data_len;
    uint8_t *data;
};

/* core API */
int   message_serialize(struct Message *msg, uint8_t *buf, int maxlen);
int message_deserialize(struct Message *msg, uint8_t *buf);
struct Message* message_upgrade(struct Message *msg, uint8_t *data, int len);
void  free_message(struct Message *msg);

/* helper to print text payload */
int print_message_text(const struct Message *msg);

#endif /* SRC_PROTOCOL */