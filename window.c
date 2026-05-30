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
#define MAX_PDU_SIZE 1024

typedef struct {
    uint32_t seqNum;
    int pduLen;
    int valid;
    char pdu[MAX_PDU_SIZE];
} WindowPacket;

typedef struct{
    WindowPacket *packets;
    uint32_t windowSize;
    uint32_t lower;
    uint32_t upper;
    uint32_t current;
} Window;

Window *windowSetUp(uint32_t windowSize){
    Window *window = malloc(sizeof(Window));
    if (window == NULL) {
        fprintf(stderr, "Error allocating memory for window\n");
        return NULL;
    }

    window->packets = calloc(windowSize, sizeof(WindowPacket));
    if (window->packets == NULL) {
        fprintf(stderr, "Error allocating memory for window packets\n");
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
        fprintf(stderr, "Window is NULL\n");
        return false;
    }
    return window->current < window->upper;
}

//THIS WILL PROBABLY NEED TO BE FIXED
uint32_t getNextSeqNum(Window *window){
    if (window == NULL) {
        fprintf(stderr, "Window is NULL\n");
        return 0; // or some error code
    }
    return window->current;
}

uint32_t window_lowest_seq(Window *window) {
    return window->lower;
}

int windowAddPacket(Window *window, char *pdu, int pduLen) {
    int index;
    
}



