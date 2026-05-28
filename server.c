#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gethostbyname.h"
#include "networks.h"
#include "createPDU.h"
#include "cpe464.h"
#include "safeUtil.h"

#define MAXBUF 80
#define MAXPDU 1500