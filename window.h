#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

Window *windowSetUp(uint32_t windowSize);
void windowTeardown(Window *window);
int addWindowPacket(Window *window, char *pdu, int pduLen);
int getWindowPacket(Window *window, uint32_t seqNum, char *outPDU);
int getLowestWindowPacket(Window *window, uint32_t *seqNum, char *outPDU);
void processRR(Window *window, uint32_t RRSeqNum);
bool isWindowOpen(Window *window);

#endif 