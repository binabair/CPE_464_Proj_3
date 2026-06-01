#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "networks.h"
#include "cpe464.h"
#include "checksum.h"
#include "createPDU.h"
#include "buffer.h"
#include "pollLib.h"

#define FLAG_DATA 3
#define FLAG_FILENAME 7
#define FLAG_FILENAME_RESPONSE 8
#define FLAG_EOF 32
#define FLAG_EOF_ACK 33
#define FLAG_FINAL_ACK 34

#define MAX_PDU_SIZE 1407
#define HEADER_LEN 7

typedef struct {
    int socketNum;
    int outputFd;
    int windowSize;
    int bufferSize;
    char toFile[101];
    struct sockaddr_in6 client;
    socklen_t clientLen;
    uint32_t controlSeqNum;
} ServerInfo;

void processClient(int parentSocket, uint8_t *firstPDU, int firstLen, struct sockaddr_in6 *client, socklen_t clientLen, double errorRate);
int parseFilenamePacket(ServerInfo *info, uint8_t *pdu, int pduLen);
int sendFilenameResponse(ServerInfo *info, uint8_t status);
int sendServerControlPacket(ServerInfo *info, uint8_t flag, uint8_t *payload, int payloadLen);
void receiveFile(ServerInfo *info);
int checkArgs(int argc, char *argv[]);
void handleValidFilenamePacket(int socketNum, uint8_t *pdu, int pduLen, struct sockaddr_in6 *client, socklen_t clientLen, double errorRate);


void processClient(int socketNum, uint8_t *firstPDU, int firstLen, struct sockaddr_in6 *client, socklen_t clientLen, double errorRate){
    ServerInfo info;
    uint8_t status;

    memset(&info, 0, sizeof(info));

    close(socketNum); //close the parents so the child can get its own
    info.socketNum = udpServerSetup(0);

    info.client = *client;
    info.clientLen = clientLen;
    info.controlSeqNum = 0;

    if (parseFilenamePacket(&info, firstPDU, firstLen) < 0){
        close(info.socketNum);
        return;
    }

    info.outputFd = open(info.toFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (info.outputFd < 0){
        status = 1;
        sendFilenameResponse(&info, status);
        close(info.socketNum);
        return;
    }

    status = 0;
    sendFilenameResponse(&info, status);

    bufferInit(info.windowSize);

    setupPollSet();
    addToPollSet(info.socketNum);

    receiveFile(&info);

    bufferFree();
    close(info.outputFd);
    close(info.socketNum);
}

int sendServerControlPacket(ServerInfo *info, uint8_t flag, uint8_t *payload, int payloadLen){
    uint8_t pdu[MAX_PDU_SIZE];
    int pduLen;

    pduLen = createPDU(pdu, info->controlSeqNum++, flag, payload, payloadLen);

    return sendtoErr(info->socketNum, pdu, pduLen, 0, (struct sockaddr *)&info->client, info->clientLen);
}

int sendFilenameResponse(ServerInfo *info, uint8_t status){
    return sendServerControlPacket(info, FLAG_FILENAME_RESPONSE, &status, 1);
}

void receiveFile(ServerInfo *info){
    uint8_t pdu[MAX_PDU_SIZE];
    int pduLen;
    int pollResult;
    int flag;
    int EOFTries = 0;

    while (1) {
        pollResult = pollCall(10000);

        if (pollResult != info->socketNum){
            return;
        }

        pduLen = recvfromErr(info->socketNum, pdu, MAX_PDU_SIZE, 0, NULL, NULL);

        if (pduLen <= 0 || !validateChecksum(pdu, pduLen)){
            continue;
        }

        flag = getFlag(pdu);

        if (flag == FLAG_DATA){
            bufferProcessPDU(info->socketNum, info->outputFd, pdu, pduLen, (struct sockaddr *)&info->client,info->clientLen);
        } else if (flag == FLAG_EOF) {
            sendServerControlPacket(info, FLAG_EOF_ACK, NULL, 0);

            while (EOFTries < 10) {
                pollResult = pollCall(1000);

                if (pollResult == info->socketNum){
                    pduLen = recvfromErr(info->socketNum, pdu, MAX_PDU_SIZE, 0, NULL, NULL);

                    if (pduLen > 0 && validateChecksum(pdu, pduLen) && getFlag(pdu) == FLAG_FINAL_ACK){
                        return;
                    }
                }else{
                    sendServerControlPacket(info, FLAG_EOF_ACK, NULL, 0);
                    EOFTries++;
                }
            }
            return;
        }
    }
}

int parseFilenamePacket(ServerInfo *info, uint8_t *pdu, int pduLen){
    int offset;
    uint32_t netWindow;
    uint32_t netBuffer;

    if (pduLen < HEADER_LEN + 8 + 1){
        return -1;
    }

    offset = HEADER_LEN;

    memcpy(&netWindow, pdu + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    memcpy(&netBuffer, pdu + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    info->windowSize = ntohl(netWindow);
    info->bufferSize = ntohl(netBuffer);

    strncpy(info->toFile, (char *)(pdu + offset), 100);
    info->toFile[100] = '\0';

    return 0;
}


int checkArgs(int argc, char *argv[]){
    int portNumber;

    if (argc < 2 || argc > 3){
        printf("usage: %s error-rate [optional-port-number]\n", argv[0]);
        exit(1);
    }

    if (argc == 3){
        portNumber = atoi(argv[2]);
    }
    else {
        portNumber = 0;
    }

    return portNumber;
}

int setupServer(int portNumber, double errorRate){
    int socketNum;

    sendtoErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_OFF, RSEED_ON);

    socketNum = udpServerSetup(portNumber);

    return socketNum;
}

void runServer(int socketNum, double errorRate){
    uint8_t pdu[MAX_PDU_SIZE];
    int pduLen;
    struct sockaddr_in6 client;
    socklen_t clientLen;

    while (1) {
        clientLen = sizeof(client);

        pduLen = recvfromErr(socketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *)&client, &clientLen);

        if (pduLen <= 0) {
            continue;
        }

        if (!validateChecksum(pdu, pduLen)) {
            continue;
        }

        if (getFlag(pdu) != FLAG_FILENAME) {
            continue;
        }

        handleValidFilenamePacket(socketNum, pdu, pduLen, &client, clientLen, errorRate);

        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }
    }
}

void handleValidFilenamePacket(int socketNum, uint8_t *pdu, int pduLen, struct sockaddr_in6 *client, socklen_t clientLen, double errorRate){
    pid_t pid;
    pid = fork();

    if (pid == 0) {
        sendtoErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_OFF, RSEED_ON);
        processClient(socketNum, pdu, pduLen, client, clientLen, errorRate);

        exit(0);
    }
}

int main(int argc, char *argv[]){
    int socketNum;
    int portNumber;
    double errorRate;

    portNumber = checkArgs(argc, argv);
    errorRate = atof(argv[1]);

    socketNum = setupServer(portNumber, errorRate);
    runServer(socketNum, errorRate);

    close(socketNum);
    return 0;
}