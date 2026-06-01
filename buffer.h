#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>
#include <sys/socket.h>

#define MAX_DATA_SIZE 1400

typedef enum {
    INORDER,
    BUFFERING,
    FLUSHING
} BufferState;

typedef struct {
    uint32_t seqNum;
    int dataLen;
    int validFlag;
    uint8_t data[MAX_DATA_SIZE];
} BufferEntry;

void bufferInit(int size);
void bufferProcessPDU( int socketNum, int outputFd, uint8_t *pdu, int pduLen, struct sockaddr *client, socklen_t clientLen);
void bufferFree(void);
BufferState inOrderState( int socketNum, int outputFd, uint32_t recvSeq, uint8_t *payload, int payloadLen, struct sockaddr *client, socklen_t clientLen);
BufferState bufferingState( int socketNum, int outputFd, uint32_t recvSeq, uint8_t *payload, int payloadLen, struct sockaddr *client, socklen_t clientLen);
void sendRR( int socketNum, struct sockaddr *client, socklen_t clientLen);
void sendSREJ( int socketNum, struct sockaddr *client, socklen_t clientLen);
void writePacketToDisk(int outputFd, uint8_t *data, int len);
void bufferPacket(uint32_t seqNum, uint8_t *data, int dataLen);
int getIndex(uint32_t seqNum);
void sendControl(int socketNum, struct sockaddr *client, socklen_t clientLen, uint8_t flag, uint32_t controlSeq);

#endif