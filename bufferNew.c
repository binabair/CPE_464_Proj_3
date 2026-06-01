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

BufferEntry *buffer = NULL;
int windowSize = 0;
uint32_t expected = 0;
uint32_t highest = 0;
BufferState state = INORDER;
uint32_t ackSeqNum = 0;

void bufferInit(int size){
    int i;

    windowSize = size;
    expected = 0;
    highest = 0;
    state = INORDER;
    ackSeqNum = 0;

    buffer = malloc(sizeof(BufferEntry) * windowSize);

    for (i = 0; i < windowSize; i++) {
        buffer[i].validFlag = 0;
        buffer[i].dataLen = 0;
        buffer[i].seqNum = 0;
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

    switch (state) {
        case INORDER:
            state = inOrderState(
                socketNum,
                outputFd,
                recvSeq,
                payload,
                payloadLen,
                client,
                clientLen
            );
            break;

        case BUFFERING:
            state = bufferingState(
                socketNum,
                outputFd,
                recvSeq,
                payload,
                payloadLen,
                client,
                clientLen
            );
            break;

        case FLUSHING:
            state = flushingState(
                socketNum,
                outputFd,
                client,
                clientLen
            );
            break;
    }
}

BufferState inOrderState(int socketNum, int outputFd, uint32_t recvSeq, uint8_t *payload, int payloadLen, struct sockaddr *client, socklen_t clientLen){
    if (recvSeq == expected) {
        writePacketToDisk(outputFd, payload, payloadLen);
        highest = expected;
        expected++;

        sendRR(socketNum, client, clientLen);
        return INORDER;
    }

    if (recvSeq > expected) {
        bufferPacket(recvSeq, payload, payloadLen);
        highest = recvSeq;

        sendSREJ(socketNum, client, clientLen);
        return BUFFERING;
    }

    sendRR(socketNum, client, clientLen);
    return INORDER;
}


BufferState bufferingState(int socketNum, int outputFd, uint32_t recvSeq, uint8_t *payload, int payloadLen, struct sockaddr *client, socklen_t clientLen) {
    if (recvSeq == expected) {
        writePacketToDisk(outputFd, payload, payloadLen);
        expected++;

        return flushingState(socketNum, outputFd, client, clientLen);
    }

    if (recvSeq > expected) {
        bufferPacket(recvSeq, payload, payloadLen);

        if (recvSeq > highest) {
            highest = recvSeq;
        }

        sendSREJ(socketNum, client, clientLen);
        return BUFFERING;
    }

    sendRR(socketNum, client, clientLen);
    return BUFFERING;
}

static BufferState flushingState(int socketNum, int outputFd, struct sockaddr *client, socklen_t clientLen){
    int index;

    while (1) {
        index = getIndex(expected);

        if (buffer[index].validFlag == 0 ||
            buffer[index].seqNum != expected) {
            break;
        }

        writePacketToDisk(outputFd, buffer[index].data, buffer[index].dataLen);
        buffer[index].validFlag = 0;
        expected++;
    }

    if (expected <= highest) {
        sendSREJ(socketNum, client, clientLen);
        return BUFFERING;
    }

    sendRR(socketNum, client, clientLen);
    return INORDER;
}


void bufferFree(void){
    free(buffer);
    buffer = NULL;
}

static int getIndex(uint32_t seqNum){
    return seqNum % windowSize;
}

static void sendControl(int socketNum, struct sockaddr *client, socklen_t clientLen, uint8_t flag, uint32_t controlSeq){
    uint8_t pdu[HEADER_LEN + sizeof(uint32_t)];
    uint32_t netSeq = htonl(controlSeq);

    int pduLen = createPDU(
        pdu,
        ackSeqNum++,
        flag,
        (uint8_t *)&netSeq,
        sizeof(uint32_t)
    );

    sendtoErr(socketNum, pdu, pduLen, 0, client, clientLen);
}

void sendRR(int socketNum, struct sockaddr *client, socklen_t clientLen){
    sendControl(socketNum, client, clientLen, FLAG_RR, expected);
}

void sendSREJ(int socketNum, struct sockaddr *client, socklen_t clientLen){
    sendControl(socketNum, client, clientLen, FLAG_SREJ, expected);
}

void writePacketToDisk(int outputFd, uint8_t *data, int len){
    write(outputFd, data, len);
}

void bufferPacket(uint32_t seqNum, uint8_t *data, int dataLen){
    int index = getIndex(seqNum);

    buffer[index].seqNum = seqNum;
    buffer[index].dataLen = dataLen;
    buffer[index].validFlag = 1;
    memcpy(buffer[index].data, data, dataLen);
}