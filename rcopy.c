// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

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

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "createPDU.h"
#include "cpe464.h"

#define MAXBUF 80
#define MAXPDU 1500

void talkToServer(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);
void checkErrorRate(double errorRate);

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	double errorRate = 0;
	
	portNumber = checkArgs(argc, argv);

	errorRate = atof(argv[1]);
	checkErrorRate(errorRate);
	sendtoErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
	
	socketNum = setupUdpClientToServer(&server, argv[2], portNumber);
	
	talkToServer(socketNum, &server);
	
	close(socketNum);

	return 0;
}

void talkToServer(int socketNum, struct sockaddr_in6 * server)
{
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char * ipString = NULL;
	int dataLen = 0; 
	char buffer[MAXBUF+1];

	int pduLen = 0;
	uint8_t pduBuffer[MAXPDU];
	uint32_t seqNum = 0;
	uint8_t flag = 1;
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin(buffer);

		pduLen = createPDU(pduBuffer, seqNum, flag, (uint8_t *)buffer, dataLen);
        seqNum++;

		printf("Sending PDU:\n");
        printPDU(pduBuffer, pduLen);
	
		safeSendto(socketNum, pduBuffer, pduLen, 0, (struct sockaddr *) server, serverAddrLen);
		
		pduLen = safeRecvfrom(socketNum, pduBuffer, MAXPDU, 0, (struct sockaddr *) server, &serverAddrLen);

		ipString = ipAddressToString(server);
		
        printf("Server with ip: %s and port %d sent back:\n",
               ipString, ntohs(server->sin6_port));

        printPDU(pduBuffer, pduLen);
	    
	}
}

int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
}

int checkArgs(int argc, char * argv[]){
    int portNumber = 0;
    /* check command line arguments  */
	
	if (argc != 4)
	{
		printf("usage: %s error-rate host-name port-number \n", argv[0]);
		exit(1);
	}

	portNumber = atoi(argv[3]);

	return portNumber;
}

void checkErrorRate(double errorRate)
{
    if (errorRate < 0 || errorRate >= 1)
    {
        fprintf(stderr, "Error rate must be >= 0 and < 1\n");
        exit(1);
    }
}





