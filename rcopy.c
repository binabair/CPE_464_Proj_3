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
	
	if (argc != 8)
	{
		printf("usage: %s from-file to-file window-size buffer-size error-rate remote-machine remote-port\n", argv[0]);
		exit(1);
	}

	return argv;
}

void checkErrorRate(double errorRate)
{
    if (errorRate < 0 || errorRate >= 1)
    {
        fprintf(stderr, "Error rate must be >= 0 and < 1\n");
        exit(1);
    }
}

