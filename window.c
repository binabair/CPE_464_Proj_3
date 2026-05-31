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
#include <stdbool.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h" 
#include "checksum.h" 
#include "window.h"

//sender does windowing
//creating the sliding window protocol header and payload

Window *windowSetUp(uint32_t windowSize){
    Window *window = malloc(sizeof(Window));
    if (window == NULL) {
        return NULL;
    }

    window->packets = calloc(windowSize, sizeof(WindowPacket));
    if (window->packets == NULL) {
        free(window);
        return NULL;
    }

    window->windowSize = windowSize;
    window->lower = 0;
    window->current = 0;
    window->upper = windowSize;

    return window;
}

void windowTeardown(Window *window){
    if (window != NULL) {
        free(window->packets);
        free(window);
    }
}

bool isWindowOpen(Window *window){
    if (window == NULL) {
        return false;
    }
    return window->current < window->upper;
}

//LOOK HERE IF SHIT GOES POORLY THIS WILL PROBABLY NEED TO BE FIXED
uint32_t getNextSeqNum(Window *window){
    if (window == NULL) {
        return 0; 
    }
    return window->current;
}

uint32_t window_lowest_seq(Window *window) {
    return window->lower;
}

int windowAddPacket(Window *window, char *pdu, int pduLen) {
    int index;

    //error check that hoe
    if (window == NULL) { //window doesnt actually exist yet
        return -1;
    }

    if (!isWindowOpen(window)) { //window closed
        return -1;
    }

    if (pduLen <= 0 || pduLen > MAX_PDU_SIZE) { //bad pdu length
        return -1;
    }

    //add packet to window
    index = window->current % window->windowSize;
    window->packets[index].seqNum = window->current;
    window->packets[index].pduLen = pduLen;
    window->packets[index].active = 1;
    memcpy(window->packets[index].pdu, pdu, pduLen);

    window->current++;
    return 0;
}

int getWindowPacket(Window *window, uint32_t seqNum, char *outPDU){
    int index;
    WindowPacket *packet;

    if (window == NULL || outPDU == NULL ||seqNum < window->lower || seqNum >= window->current) {
        return -1;
    }

    index = seqNum % window->windowSize;
    packet = &window->packets[index];

    if (packet->active == 0 || packet->seqNum != seqNum) {
        return -1;
    }

    memcpy(outPDU, packet->pdu, packet->pduLen);
    return packet->pduLen;
}

int getLowestWindowPacket(Window *window, uint32_t *seqNum, char *outPDU){
    int pduLen;

    if (window == NULL || outPDU == NULL || seqNum == NULL) {
        return -1;
    }

    if (window->lower == window->current) { //there arent any packets
        return -1;
    }

    pduLen = getWindowPacket(window, window->lower, outPDU);
    if (pduLen < 0) {
        return -1;
    }

    *seqNum = window->lower;
    return pduLen;
}

void processRR(Window *window, uint32_t RRSeqNum){
    uint32_t seq;
    uint32_t index;

    if (window == NULL || RRSeqNum <= window->lower) {
        return;
    }

    if (RRSeqNum > window->current){
        RRSeqNum = window->current;
    }

    for (seq = window->lower; seq < RRSeqNum; seq++) {
        index = seq % window->windowSize;
        window->packets[index].active = 0;
    }

    window->lower = RRSeqNum;
    window->upper = window->lower + window->windowSize;
}



