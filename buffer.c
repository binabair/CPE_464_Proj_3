//manages the buffer for received data, sends back RRs and SREJs 
//and writes data to the output file in the correct order
//reciever does buffering

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include "window.h"
#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "checksum.h"
#include "buffer.h"

#define MAX_DATA_SIZE 1400


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

Buffer *bufferSetUp(uint32_t bufferSize, int outputFD){
    Buffer *buffer = malloc(sizeof(Buffer));
    if (buffer == NULL) {
        return NULL;
    }

    buffer->packets = calloc(bufferSize, sizeof(BufferPacket));
    if (buffer->packets == NULL) {
        free(buffer);
        return NULL;
    }

    buffer->bufferSize = bufferSize;
    buffer->lower = 0;
    buffer->upper = bufferSize;
    buffer->outputFD = outputFD;
    return buffer;
}

void bufferTeardown(Buffer *buffer){
    if (buffer != NULL) {
        free(buffer->packets);
        free(buffer);
    }
}

int inBufferWindow(Buffer *buffer, uint32_t seqNum){
    if (buffer == NULL) {
        return 0;
    }
    return (seqNum >= buffer->lower && seqNum < buffer->upper);
}

int writeData(Buffer *buffer, char *data, int dataLen){
    int bytesWritten = 0;

    bytesWritten = write(buffer->outputFD, data, dataLen);
    if (bytesWritten < 0) {
        perror("write error");
        return -1;
    }

    if (bytesWritten != dataLen) {
        fprintf(stderr, "Partial write\n");
        return -1;
    }

    return 0;
}

int bufferStorePacket(Buffer *buffer, uint32_t seqNum, char *data, int dataLen){
    uint32_t index;
    BufferPacket *packet;

    if(buffer == NULL || data == NULL){
        return -1;
    }

    if(!inBufferWindow(buffer, seqNum)){
        return -1;
    }

    if (dataLen <= 0 || dataLen > MAX_DATA_SIZE) {
        return -1;
    }

    index = seqNum % buffer->bufferSize;
    packet = &buffer->packets[index];
    packet->seqNum = seqNum;
    packet->dataLen = dataLen;
    packet->active = 1;
    memcpy(packet->data, data, dataLen);
    return 0;
}

int bufferHasPacket(Buffer *buffer, uint32_t seqNum){
    uint32_t index;
    BufferPacket *packet;

    if (buffer == NULL || !inBufferWindow(buffer, seqNum)) {
        return 0;
    }

    index = seqNum % buffer->bufferSize;
    packet = &buffer->packets[index];
    return (packet->active && packet->seqNum == seqNum);
}

int bufferWriteSlide(Buffer *buffer){
    uint32_t index;
    BufferPacket *packet;

    while (bufferHasPacket(buffer, buffer->lower)) {
        index = buffer->lower % buffer->bufferSize;
        packet = &buffer->packets[index];

        if (writeData(buffer, packet->data, packet->dataLen) < 0) {
            return -1;
        }

        packet->active = 0; //inactive after writing
        buffer->lower++;
        buffer->upper++;
    }
    return 0;
}

uint32_t getLower(Buffer *buffer){
    if (buffer == NULL) {
        return 0;
    }
    return buffer->lower;
}

uint32_t getUpper(Buffer *buffer){
    if (buffer == NULL) {
        return 0;
    }
    return buffer->upper;
}

int processData(Buffer *buffer, uint32_t seqNum, char *data, int dataLen, uint32_t *responseSeq){
    if (buffer == NULL || data == NULL || responseSeq == NULL) {
        return -1;
    }

    //packet below lower, we alredy got it so send RR for current
    if (seqNum < buffer->lower) {
        *responseSeq = buffer->lower;
        return SEND_RR;
    }
    
    //packet outside window
    if (seqNum >= buffer->upper) {
        return SEND_IGNORE;
    }

    //packet is expected, do the normal things
    if (seqNum == buffer->lower){
        if (writeData(buffer, data, dataLen) < 0) {
            return -1;
        }
        buffer->lower++;
        buffer->upper = buffer->lower + buffer->bufferSize;

        if (bufferWriteSlide(buffer) < 0) {
            return -1;
        }

        *responseSeq = buffer->lower;
        return SEND_RR;
    }

    //packet higher than exoected, buffer it ad sent SREJ
    if (seqNum > buffer->lower){
        if(!bufferHasPacket(buffer, seqNum)){
            if (bufferStorePacket(buffer, seqNum, data, dataLen) < 0) {
                return -1;
            }
        }
        *responseSeq = seqNum;
        return SEND_SREJ;
    }
    return SEND_IGNORE;
}