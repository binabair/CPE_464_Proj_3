#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>

#define MAX_DATA_SIZE 1400

#define SEND_IGNORE 0
#define SEND_RR     1
#define SEND_SREJ   2

typedef enum {
    INORDER,
    BUFFERING,
    FLUSHING
} BufferState;

typedef struct {
    uint32_t seqNum;
    int dataLen;
    int active;
    char data[MAX_DATA_SIZE];
} BufferPacket;

typedef struct {
    BufferPacket *packets;
    uint32_t bufferSize;
    uint32_t lower;
    uint32_t upper;
    int outputFD;
} Buffer;

Buffer *bufferSetUp(uint32_t bufferSize, int outputFD);
void bufferTeardown(Buffer *buffer);

int inBufferWindow(Buffer *buffer, uint32_t seqNum);
int writeData(Buffer *buffer, char *data, int dataLen);
int bufferStorePacket(Buffer *buffer, uint32_t seqNum, char *data, int dataLen);
int bufferHasPacket(Buffer *buffer, uint32_t seqNum);
int bufferWriteSlide(Buffer *buffer);

uint32_t getLower(Buffer *buffer);
uint32_t getUpper(Buffer *buffer);

int processData(
    Buffer *buffer,
    uint32_t seqNum,
    char *data,
    int dataLen,
    uint32_t *responseSeq
);

#endif