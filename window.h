#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_PDU_SIZE 1407

typedef struct {
    uint32_t seqNum;
    int pduLen;
    int active; // 1 if packet has yet to be RRd, 0 if valid RR came through
    char pdu[MAX_PDU_SIZE];
} WindowPacket;

typedef struct{
    WindowPacket *packets;
    uint32_t windowSize;
    uint32_t lower;
    uint32_t upper;
    uint32_t current;
} Window;

Window *windowSetUp(uint32_t windowSize);
void windowTeardown(Window *window);
int addWindowPacket(Window *window, char *pdu, int pduLen);
int getWindowPacket(Window *window, uint32_t seqNum, char *outPDU);
int getLowestWindowPacket(Window *window, uint32_t *seqNum, char *outPDU);
void processRR(Window *window, uint32_t RRSeqNum);
bool isWindowOpen(Window *window);
int processSREJ(Window *window, uint32_t SREJSeqNum, char *outPDU);

#endif 