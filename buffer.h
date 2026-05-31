#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>

#define SEND_RR 1
#define SEND_SREJ 2
#define SEND_IGNORE 3

typedef struct Buffer Buffer;

Buffer *bufferSetUp(uint32_t bufferSize, int outputFd);
void bufferTeardown(Buffer *buffer);
int inBufferWindow(Buffer *buffer, uint32_t seqNum);
int writeData(Buffer *buffer, char *data, int dataLen);
int bufferStorePacket(Buffer *buffer, uint32_t seqNum, char *data, int dataLen);
int bufferHasPacket(Buffer *buffer, uint32_t seqNum);
int bufferWriteSlide(Buffer *buffer);
uint32_t getLower(Buffer *buffer);
uint32_t getUpper(Buffer *buffer);
int processData(Buffer *buffer, uint32_t seqNum, const char *data, int dataLen, uint32_t *responseSeq);

#endif