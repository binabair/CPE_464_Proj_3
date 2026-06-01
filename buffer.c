//manages the buffer for received data, sends back RRs and SREJs 
//and writes data to the output file in the correct order
//reciever does buffering

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "buffer.h"
#include "cpe464.h"
#include "checksum.h"

#define HEADER_LEN 7
#define FLAG_DATA 3
#define FLAG_RR   5
#define FLAG_SREJ 6

#define MAX_DATA_SIZE 1400

int createPDU(uint8_t *pduBuffer, uint32_t seqNum, uint8_t flag, uint8_t *payload, int payloadLen);
int validateChecksum(uint8_t *pduBuffer, int pduLen);
int getSequenceNum(uint8_t *pduBuffer);
int getFlag(uint8_t *pduBuffer);
int getPayloadLen(int pduLen);
BufferState flushingState(int socketNum, int outputFd, struct sockaddr *client, socklen_t clientLen);
int getIndex(uint32_t seqNum);
void sendSREJOnce(int socketNum, struct sockaddr *client, socklen_t clientLen);

typedef struct{
    BufferEntry *buffer;
    uint32_t windowSize;
    uint32_t expected;
    uint32_t highest;
    BufferState state;
    uint32_t ackSeqNum;
    uint32_t lastSREJ;
} BufferContext;

static BufferContext context;

void bufferInit(int size){
    int i;

    context.windowSize = size;
    context.expected = 0;
    context.highest = 0;
    context.state = INORDER;
    context.ackSeqNum = 0;
    context.lastSREJ = UINT32_MAX;

    context.buffer = malloc(sizeof(BufferEntry) * context.windowSize);

    for (i = 0; i < context.windowSize; i++) {
        context.buffer[i].validFlag = 0;
        context.buffer[i].dataLen = 0;
        context.buffer[i].seqNum = 0;
    }
}

void bufferProcessPDU(int socketNum, int outputFd, uint8_t *pdu, int pduLen, struct sockaddr *client, socklen_t clientLen){
    uint32_t recvSeq;
    int flag;
    int payloadLen;
    uint8_t *payload;

    if (!validateChecksum(pdu, pduLen)) {
        return;
    }

    flag = getFlag(pdu);

    if (flag != FLAG_DATA) {
        return;
    }

    recvSeq = getSequenceNum(pdu);
    payloadLen = getPayloadLen(pduLen);
    payload = pdu + HEADER_LEN;

    switch (context.state) {
        case INORDER:
            context.state = inOrderState(socketNum, outputFd, recvSeq, payload, payloadLen, client, clientLen);
            break;

        case BUFFERING:
            context.state = bufferingState(socketNum, outputFd, recvSeq, payload, payloadLen, client, clientLen);
            break;

        case FLUSHING:
            context.state = flushingState(socketNum, outputFd, client, clientLen);
            break;
    }
}

BufferState inOrderState(int socketNum, int outputFd, uint32_t recvSeq, uint8_t *payload, int payloadLen, struct sockaddr *client, socklen_t clientLen){
    if (recvSeq == context.expected) {
        writePacketToDisk(outputFd, payload, payloadLen);
        context.highest = context.expected;
        context.expected++;

        sendRR(socketNum, client, clientLen);
        return INORDER;
    }

    if (recvSeq > context.expected) {
        bufferPacket(recvSeq, payload, payloadLen);
        context.highest = recvSeq;

        sendSREJOnce(socketNum, client, clientLen);
        return BUFFERING;
    }

    sendRR(socketNum, client, clientLen);
    return INORDER;
}


BufferState bufferingState(int socketNum, int outputFd, uint32_t recvSeq, uint8_t *payload, int payloadLen, struct sockaddr *client, socklen_t clientLen) {
    if (recvSeq == context.expected) {
        context.lastSREJ = UINT32_MAX;
        writePacketToDisk(outputFd, payload, payloadLen);
        context.expected++;
        return flushingState(socketNum, outputFd, client, clientLen);
    }

    if (recvSeq > context.expected) {
        bufferPacket(recvSeq, payload, payloadLen);

        if (recvSeq > context.highest) {
            context.highest = recvSeq;
        }

        sendSREJOnce(socketNum, client, clientLen);
        return BUFFERING;
    }

    sendRR(socketNum, client, clientLen);
    return BUFFERING;
}

BufferState flushingState(int socketNum, int outputFd, struct sockaddr *client, socklen_t clientLen){
    int index;

    while (1) {
        index = getIndex(context.expected);

        if (context.buffer[index].validFlag == 0 ||
            context.buffer[index].seqNum != context.expected) {
            break;
        }

        writePacketToDisk(outputFd, context.buffer[index].data, context.buffer[index].dataLen);
        context.buffer[index].validFlag = 0;
        context.expected++;
    }

    if (context.expected <= context.highest) {
        sendSREJOnce(socketNum, client, clientLen);
        return BUFFERING;
    }

    sendRR(socketNum, client, clientLen);
    return INORDER;
}


void bufferFree(void){
    free(context.buffer);
    context.buffer = NULL;
}

int getIndex(uint32_t seqNum){
    return seqNum % context.windowSize;
}

void sendControl(int socketNum, struct sockaddr *client, socklen_t clientLen, uint8_t flag, uint32_t controlSeq){
    uint8_t pdu[HEADER_LEN + sizeof(uint32_t)];
    uint32_t netSeq = htonl(controlSeq);

    int pduLen = createPDU(
        pdu,
        context.ackSeqNum++,
        flag,
        (uint8_t *)&netSeq,
        sizeof(uint32_t)
    );

    sendtoErr(socketNum, pdu, pduLen, 0, client, clientLen);
}

void sendRR(int socketNum, struct sockaddr *client, socklen_t clientLen){
    sendControl(socketNum, client, clientLen, FLAG_RR, context.expected);
}

void sendSREJ(int socketNum, struct sockaddr *client, socklen_t clientLen){
    sendControl(socketNum, client, clientLen, FLAG_SREJ, context.expected);
}

void writePacketToDisk(int outputFd, uint8_t *data, int len){
    write(outputFd, data, len);
}

void sendSREJOnce(int socketNum, struct sockaddr *client, socklen_t clientLen)
{
    if (context.lastSREJ != context.expected) {
        sendSREJ(socketNum, client, clientLen);
        context.lastSREJ = context.expected;
    }
}

void bufferPacket(uint32_t seqNum, uint8_t *data, int dataLen){
    int index = getIndex(seqNum);

    context.buffer[index].seqNum = seqNum;
    context.buffer[index].dataLen = dataLen;
    context.buffer[index].validFlag = 1;
    memcpy(context.buffer[index].data, data, dataLen);
}