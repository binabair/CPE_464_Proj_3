#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h" 
#include "checksum.h" 

int createPDU(uint8_t *pduBuffer, uint32_t seqNum, uint8_t flag, uint8_t * payload, int payloadLen){
    uint32_t sequenceNum = htonl(seqNum);
    memcpy(pduBuffer, &sequenceNum, sizeof(sequenceNum)); 

    //set the checksum to zero first
    pduBuffer[4] = 0;
    pduBuffer[5] = 0;

    pduBuffer[6] = flag;

    memcpy(pduBuffer + 7, payload, payloadLen);
    // calculate checksum and put it in the buffer
    uint16_t checksum = in_cksum((unsigned short *)pduBuffer, 7 + payloadLen);
    memcpy(pduBuffer + 4, &checksum, sizeof(checksum));
    return 7 + payloadLen;
}

void printPDU(uint8_t *pduBuffer, int pduLen){
    uint32_t seqNum;
    uint16_t checksum;
    uint8_t flag;
    uint8_t *payload;
    int payloadLen = pduLen - 7;

    if(pduLen < 7){
        printf("ERROR: PDU too short\n");
        return;
    }

    if (in_cksum((unsigned short *)pduBuffer, pduLen) != 0){
        printf("ERROR: PDU is corrupted - checksum failed\n");
    }

    memcpy(&seqNum, pduBuffer, sizeof(seqNum));
    seqNum = ntohl(seqNum); // convert back to host order

    memcpy(&checksum, pduBuffer + 4, sizeof(checksum));
    memcpy(&flag, pduBuffer + 6, sizeof(flag));

    payload = pduBuffer + 7; // payload starts after the header

    printf("Sequence Number: %u\n", seqNum);
    printf("Checksum: %u\n", checksum);
    printf("Flag: %u\n", flag);
    printf("Payload Length: %d\n", payloadLen);
    printf("Payload: %.*s\n", payloadLen, payload);
}