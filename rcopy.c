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
#include <stdint.h>
#include <arpa/inet.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "createPDU.h"
#include "cpe464.h"
#include "checksum.h"
#include "window.h"
#include "pollLib.h"

#define FLAG_DATA 3
#define FLAG_RR   5
#define FLAG_SREJ 6
#define FLAG_FILENAME 7
#define FLAG_FILENAME_RESPONSE 8
#define FLAG_EOF 32
#define FLAG_EOF_ACK 33
#define FLAG_FINAL_ACK 34
#define MAX_PDU_SIZE 1407

typedef enum {
    SEND_FILENAME,
    WAIT_FILENAME_ACK,
    SEND_DATA,
    PROCESS_ACKS,
    WINDOW_CLOSED,
    SEND_EOF,
    WAIT_EOF_ACK,
    DONE
} RcopyState;


typedef struct {
    int socketNum;
    int inputFd;
    int bufferSize;
    int timeoutCount;
    int filenameTries;
    int EOFTries;
    char *fromFile;
    char *toFile;
    Window *window;
    struct sockaddr_in6 server;
    socklen_t serverLen;
	uint32_t controlSeqNum;
	int EOFReached;
} RcopyInfo;


int checkArgs(int argc, char * argv[]);
void checkErrorRate(double errorRate);
void stateHandler(RcopyInfo *info);
void initRcopyInfo(RcopyInfo *info, int argc, char *argv[]);
RcopyState sendFilenameState(RcopyInfo *info);
RcopyState waitFilenameAckState(RcopyInfo *info);
RcopyState sendDataState(RcopyInfo *info);
RcopyState processAcksState(RcopyInfo *info);
RcopyState windowClosedState(RcopyInfo *info);
RcopyState sendEofState(RcopyInfo *info);
RcopyState waitEofAckState(RcopyInfo *info);
uint32_t getControlSeqNum(uint8_t *pdu);
int sendControlPacket(RcopyInfo *info, uint8_t flag,uint8_t *payload, int payloadLen);



void stateHandler(RcopyInfo *info){
    RcopyState state = SEND_FILENAME;

	while (state != DONE){
		switch (state) {
			case SEND_FILENAME:
				state = sendFilenameState(info);
				break;

			case WAIT_FILENAME_ACK:
				state = waitFilenameAckState(info);
				break;

			case SEND_DATA:
				state = sendDataState(info);
				break;

			case PROCESS_ACKS:
				state = processAcksState(info);
				break;

			case WINDOW_CLOSED:
				state = windowClosedState(info);
				break;

			case SEND_EOF:
				state = sendEofState(info);
				break;

			case WAIT_EOF_ACK:
				state = waitEofAckState(info);
				break;

			case DONE:
				break;
		}
	}
}

RcopyState sendFilenameState(RcopyInfo *info){
    char payload[256];
    uint32_t netWindowSize;
    uint32_t netBufferSize;
    int offset = 0;

    netWindowSize = htonl(info->window->windowSize);
    netBufferSize = htonl(info->bufferSize);

    memcpy(payload + offset, &netWindowSize, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    memcpy(payload + offset, &netBufferSize, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    strcpy(payload + offset, info->toFile);
    offset += strlen(info->toFile) + 1;

    sendControlPacket(info, FLAG_FILENAME, (uint8_t *)payload, offset);

    return WAIT_FILENAME_ACK;
}

RcopyState waitFilenameAckState(RcopyInfo *info){
    char pdu[MAX_PDU_SIZE];
    int pduLen;
    int pollResult;
    int flag;

    pollResult = pollCall(1000);

    if (pollResult != info->socketNum){
        info->filenameTries++;

        if (info->filenameTries >= 10){
            return DONE;
        }

        return SEND_FILENAME;
    }

    pduLen = recvfromErr(info->socketNum, pdu, MAX_PDU_SIZE, 0, NULL, NULL);

    if (pduLen <= 0 || !validateChecksum((uint8_t *)pdu, pduLen)){
        return WAIT_FILENAME_ACK;
    }

    flag = getFlag((uint8_t *)pdu);

    if (flag != FLAG_FILENAME_RESPONSE){
        return WAIT_FILENAME_ACK;
    }

    /*
       Assumption:
       server sends 1 byte payload:
       0 = file opened successfully
       nonzero = file open failed
    */
    if (pduLen > 7 && pdu[7] != 0){
        printf("Error on open of output file: %s\n", info->toFile);
        return DONE;
    }

    info->filenameTries = 0;
    return SEND_DATA;
}

RcopyState sendDataState(RcopyInfo *info){
    char dataBuf[1400];
    char pdu[MAX_PDU_SIZE];
    int dataLen;
    int pduLen;
    uint32_t seqNum;

	if (info->EOFReached){
		if (window_lowest_seq(info->window) == getNextSeqNum(info->window)){
			return SEND_EOF;
		}

		return PROCESS_ACKS;
	}

    if (!isWindowOpen(info->window)){
        return WINDOW_CLOSED;
    }

    dataLen = read(info->inputFd, dataBuf, info->bufferSize);

    if (dataLen < 0){
        perror("read");
        return DONE;
    }

    if (dataLen == 0){
		info->EOFReached = 1;

		if (window_lowest_seq(info->window) == getNextSeqNum(info->window)) {
			return SEND_EOF;
		}

		return PROCESS_ACKS;
    }

    seqNum = getNextSeqNum(info->window);

    pduLen = createPDU((uint8_t *)pdu, seqNum, FLAG_DATA, (uint8_t *)dataBuf, dataLen);

    sendtoErr(info->socketNum, pdu, pduLen, 0, (struct sockaddr *)&info->server, info->serverLen );

    windowAddPacket(info->window, pdu, pduLen);

    return PROCESS_ACKS;
}


RcopyState processAcksState(RcopyInfo *info){
    char pdu[MAX_PDU_SIZE];
    int pduLen;
    int pollResult;
    uint8_t flag;
    uint32_t ackSeq;
    char resendPDU[MAX_PDU_SIZE];
    int resendLen;

    pollResult = pollCall(0);

    while (pollResult == info->socketNum){
        pduLen = recvfromErr(info->socketNum,pdu,MAX_PDU_SIZE,0,NULL,NULL);

        if (pduLen > 0 && validateChecksum((uint8_t *)pdu, pduLen)){
            flag = getFlag((uint8_t *)pdu);
            ackSeq = getControlSeqNum((uint8_t *)pdu);

            if (flag == FLAG_RR){
                processRR(info->window, ackSeq);
				info->timeoutCount = 0;
            }
            else if (flag == FLAG_SREJ){
                resendLen = processSREJ(info->window, ackSeq, resendPDU);

                if (resendLen > 0){
                    sendtoErr(info->socketNum, resendPDU, resendLen, 0, (struct sockaddr *)&info->server, info->serverLen);
                }
				info->timeoutCount = 0;
            }
        }

        pollResult = pollCall(0);
    }

	if (info->EOFReached &&
		window_lowest_seq(info->window) == getNextSeqNum(info->window)){
		return SEND_EOF;
	}

	if (isWindowOpen(info->window)){
		return SEND_DATA;
	}

	return WINDOW_CLOSED;
}

RcopyState windowClosedState(RcopyInfo *info){
    int pollResult;
    char pdu[MAX_PDU_SIZE];
    int pduLen;
    uint32_t seqNum;

    pollResult = pollCall(1000);

    if (pollResult == info->socketNum){
        info->timeoutCount = 0;
        return PROCESS_ACKS;
    }

    pduLen = getLowestWindowPacket(info->window, &seqNum, pdu);

    if (pduLen > 0){
        sendtoErr(info->socketNum, pdu, pduLen, 0, (struct sockaddr *)&info->server, info->serverLen);
    }

    info->timeoutCount++;

    if (info->timeoutCount >= 10){
        return DONE;
    }

    return WINDOW_CLOSED;
}

uint32_t getControlSeqNum(uint8_t *pdu){
    uint32_t netSeq;

    memcpy(&netSeq, pdu + 7, sizeof(uint32_t));
    return ntohl(netSeq);
}

 int sendControlPacket(RcopyInfo *info, uint8_t flag, uint8_t *payload, int payloadLen){
    char pdu[MAX_PDU_SIZE];
    int pduLen;

    pduLen = createPDU((uint8_t *)pdu, info->controlSeqNum++, flag, payload, payloadLen);

    return sendtoErr(info->socketNum, pdu, pduLen, 0, (struct sockaddr *)&info->server, info->serverLen);
}

RcopyState sendEofState(RcopyInfo *info){
    sendControlPacket(info, FLAG_EOF, NULL, 0);

    return WAIT_EOF_ACK;
}

RcopyState waitEofAckState(RcopyInfo *info){
    char pdu[MAX_PDU_SIZE];
    int pduLen;
    int pollResult;
    int flag;

    pollResult = pollCall(1000);

    if (pollResult != info->socketNum){
        info->EOFTries++;

        if (info->EOFTries >= 10){
            return DONE;
        }

        return SEND_EOF;
    }

    pduLen = recvfromErr(info->socketNum, pdu, MAX_PDU_SIZE, 0, NULL, NULL);

    if (pduLen <= 0 || !validateChecksum((uint8_t *)pdu, pduLen)){
        return WAIT_EOF_ACK;
    }

    flag = getFlag((uint8_t *)pdu);

    if (flag != FLAG_EOF_ACK){
        return WAIT_EOF_ACK;
    }

    sendControlPacket(info, FLAG_FINAL_ACK, NULL, 0);

    return DONE;
}


int checkArgs(int argc, char * argv[]){
    int portNumber = 0;
	
	if (argc != 8){
		printf("incorrect args\n");
		exit(1);
	}

	if (strlen(argv[1]) > 100 || strlen(argv[2]) > 100){
		fprintf(stderr, "file name too long\n");
		exit(1);
	}

	portNumber = atoi(argv[7]);
	return portNumber;
}

void initRcopyInfo(RcopyInfo *info, int argc, char *argv[]){
    int portNumber;
    int windowSize;
    double errorRate;

    portNumber = checkArgs(argc, argv);

    info->fromFile = argv[1];
    info->toFile = argv[2];
    windowSize = atoi(argv[3]);
    info->bufferSize = atoi(argv[4]);
    errorRate = atof(argv[5]);
	info->controlSeqNum = 0;
	info->EOFReached = 0;

    checkErrorRate(errorRate);

	if (windowSize <= 0){
		fprintf(stderr, "window size must be greater than 0\n");
		exit(1);
	}

    if (info->bufferSize < 1 || info->bufferSize > 1400){
        printf("Error: buffer-size must be between 1 and 1400\n");
        exit(1);
    }

    info->inputFd = open(info->fromFile, O_RDONLY);

    if (info->inputFd < 0) {
        printf("Error: file %s not found.\n", info->fromFile);
        exit(1);
    }

    sendtoErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);

    info->socketNum = setupUdpClientToServer(&info->server, argv[6], portNumber);
    info->serverLen = sizeof(info->server);

    setupPollSet();
    addToPollSet(info->socketNum);

    info->window = windowSetUp(windowSize);

    if (info->window == NULL){
        fprintf(stderr, "Failed to create window\n");
        exit(1);
    }

    info->timeoutCount = 0;
    info->filenameTries = 0;
    info->EOFTries = 0;
}

void checkErrorRate(double errorRate)
{
    if (errorRate < 0 || errorRate >= 1){
        fprintf(stderr, "Error rate must be >= 0 and < 1\n");
        exit(1);
    }
}


int main (int argc, char *argv[]){
    RcopyInfo info;
    initRcopyInfo(&info, argc, argv);

    stateHandler(&info);

    windowTeardown(info.window);
    close(info.inputFd);
    close(info.socketNum);

    return 0;
}

