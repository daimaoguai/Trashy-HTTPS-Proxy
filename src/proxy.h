#ifndef _PROXY_H_
#define _PROXY_H_

#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/errno.h>

//#include <sys/types.h>
//#include <arpa/inet.h>



#define PUBLIC_IP		"192.168.2.11"
#define PRIVATE_IP		"192.168.2.11"
#define BUFF_SIZE   		(1024 << 6)
#define RECVBUFF_SIZE   	BUFF_SIZE
#define MAX_CONN    		SOMAXCONN
#define SERVICE_PORT  		(8888)
#define MAX_THREAD		(1024)

#define NOTFOUND_HTML_FILE			"./src/webpage/404.html"
#define NOTFOUND_RESPONSE_BUFF_SIZE		(2048)
#define BADREQUEST_HTML_FILE			"./src/webpage/400.html"
#define BADREQUEST_RESPONSE_BUFF_SIZE		(2048)


// #define OUTPUT_MSG_TO_FILE
#define MSGOUT_FILE			"./requests/"        // folder of the output files

// proxy client handle thread arguments
struct ClientHandleArg {
    int clientSockfd;
    struct sockaddr clientSockAddr;
    socklen_t clientSocklen;
};

struct OutputFp {
    FILE *msg_fp;
	FILE *err_fp;
};


extern int clientHandle (struct ClientHandleArg *clientHandleArg);

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define errmsgout(fp, msg...)   do { \
                                    fprintf(fp, "\n*****************************************************\n" ); \
                                    fprintf(fp, "Error in \""__FILE__"\" line "STR(__LINE__)": " ); \
                                    fprintf(fp, msg); \
                                    fprintf(fp, "\nError #: %d\nMessage: %s\n\n", errno, strerror(errno)); \
                                    fflush(fp); \
                                } while (0)


extern char *notFoundResponse;
extern size_t notFoundResponseLength;
extern char *badRequestResponse;
extern size_t badRequestResponseLength;





// constants
#define HTTP_PORT				(80)
#define HTTPS_PORT				(443)
#define HOSTNAME_MAX_LENGTH		(256)
#define IPSTRING_MAX_LENGTH		(46)


# if (BUFF_SIZE < 4096) || (RECVBUFF_SIZE < 4096) || \
	 (NOTFOUND_RESPONSE_BUFF_SIZE < 1024) || (BADREQUEST_RESPONSE_BUFF_SIZE < 1024)
# warning "Buffer size might be too small"
# endif


#endif
