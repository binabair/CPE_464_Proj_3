#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>
#include <sys/socket.h>

#define MAX_PAYLOAD 1400

typedef struct {
    uint32_t seqNum;
    int dataLen;
    int validFlag;
    uint8_t data[MAX_PAYLOAD];
} BufferEntry;

typedef enum {
    INORDER,
    BUFFERING,
    FLUSHING
} BufferState;

#endif