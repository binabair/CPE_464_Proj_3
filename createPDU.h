#ifndef PDU_H
#define PDU_H

#include <stdint.h>

int createPDU(uint8_t *pduBuffer, uint32_t seqNum, uint8_t flag, uint8_t * payload, int payloadLen);
void printPDU(uint8_t *pduBuffer, int pduLen);
int validateChecksum(uint8_t *pduBuffer, int pduLen);
int getSequenceNum(uint8_t *pduBuffer);
int getFlag(uint8_t *pduBuffer);


#endif