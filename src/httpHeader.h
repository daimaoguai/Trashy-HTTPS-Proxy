#ifndef _HTTP_HEADER_H_
#define _HTTP_HEADER_H_

#include <sys/types.h>
#include "proxy.h"


typedef enum HttpConnection_Enum {
    CONNECTION_UNKNOWN = 0, CONNECTION_KEEP_ALIVE, CONNECTION_CLOSE
} HttpConnection_Enum;

typedef enum HttpMethod_Enum {
    METHOD_UNKNOWN = 0, METHOD_GET, METHOD_POST, METHOD_CONNECT, METHOD_HEAD, METHOD_PUT, METHOD_DELETE, 
    METHOD_OPTIONS, METHOD_TRACE, METHOD_PATCH
} HttpMethod_Enum;



// HTTP 头信息
struct HttpHeader {
	HttpMethod_Enum method;             // POST 或者 GET 或者 CONNECT、......
    HttpConnection_Enum connection;     // keep-alive 或者 close
    uint16_t dstPort;                   // 目标端口
    size_t recvSize;                    // 完整的请求头大小
    char *requestStr;                   // 完整的请求头
	char *absoluteUrl;                  // 请求的绝对URL 
    char *url;                          // 请求的相对URL 
	char *host;                         // 目标主机 
	char *cookie;                       // cookie 
    char httpVersion[8];                // "HTTP/1.1"、"HTTP/1.0"、......
};






extern int parseHttpHeader (char *reqBuff, size_t reqBuffSize, struct HttpHeader *httpHeader_p, struct OutputFp *outFp);
extern int getHttpHeaderFieldStr (char *buff, size_t buffSize, char *httpHeaderField_p, struct OutputFp *outFp);
extern int removeHttpHeaderElement(char *buff, char *reqBuff, char *element);



#endif