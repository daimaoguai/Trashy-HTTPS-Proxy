#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "proxy.h"
#include "httpHeader.h"

#define errout_fp   stderr
#define msgout_fp   stdout



#define PARAM_OUT
#define PARAM_IN

// Swap big endian between little endian for uint32_t
#define SWAP32(x)  ((x<<24)&0xff000000) | ((x<<8)&0xff0000) | ((x>>8)&0xff00) | ((x>>24)&0xff)


static int parseHttpMethod (struct HttpHeader *httpHeader_p, char *req_p, struct OutputFp *outFp)
{
    switch (SWAP32(*(uint32_t *)req_p)) {
        case 'GET ':
            httpHeader_p->method = METHOD_GET;
            break;

        case 'POST':
            httpHeader_p->method = METHOD_POST;
            break;

        case 'CONN':    // CONNECT
            if (*(uint64_t *)req_p == *(uint64_t *)"CONNECT ") {
                httpHeader_p->method = METHOD_CONNECT;
                break;
            } else {
                goto _httpMethod_switch_default;
            }

        case 'HEAD':
            httpHeader_p->method = METHOD_HEAD;
            break;

        case 'PUT ':
            httpHeader_p->method = METHOD_PUT;
            break;
        
        case 'DELE':    // DELETE
            if (*(uint64_t *)req_p == *(uint64_t *)"DELETE ") {
                httpHeader_p->method = METHOD_DELETE;
                break;
            } else {
                goto _httpMethod_switch_default;
            }
        
        case 'OPTI':    // OPTIONS
            if (*(uint64_t *)req_p == *(uint64_t *)"OPTIONS ") {
                httpHeader_p->method = METHOD_OPTIONS;
                break;
            } else {
                goto _httpMethod_switch_default;
            }
        
        case 'TRAC':    // TRACE
            if (*(uint16_t *)(req_p + sizeof(uint32_t)) == *(uint16_t *)"E ") {
                httpHeader_p->method = METHOD_TRACE;
                break;
            } else {
                goto _httpMethod_switch_default;
            }
        
        case 'PATC':    // PATCH
            if (*(uint16_t *) (req_p + sizeof(uint32_t)) == *(uint16_t *)"H ") {
                httpHeader_p->method = METHOD_PATCH;
                break;
            } else {
                goto _httpMethod_switch_default;
            }

        _httpMethod_switch_default:
        default:
            {
                httpHeader_p->method = METHOD_UNKNOWN;
                char *tmp = strchr(req_p, ' ');
                *tmp = '\0';
                fprintf(outFp->err_fp, "未知HTTP请求方法[%s]", req_p);
                fflush(outFp->err_fp);
                *tmp = ' ';
                return -1;
            }

    }
    return 0;
}

static int parseHttpUrl (struct HttpHeader *httpHeader_p, char *req_p, struct OutputFp *outFp)
{
    // parse absolute URL
    char *urlStrStart = strchr(req_p, ' ') + 1;
    char *urlStrEnd = strchr(urlStrStart, ' ');
    if ((urlStrStart == NULL) | (urlStrEnd == NULL)) {
        _error_parse_http_url:
        fprintf(outFp->err_fp, "解析HTTP请求头信息失败：内容并非HTTP协议头（无法解析请求URL）\n");
        fprintf(outFp->err_fp, "\"%s\"\n", req_p);
        fflush(outFp->err_fp);

        return -1;
    }
    *urlStrEnd = '\0';
    httpHeader_p->absoluteUrl = urlStrStart;
    // fprintf(outFp->msg_fp, "客户请求的绝对URL：\"%s\"\n", urlStrStart);
    // fflush(outFp->msg_fp);

    // parse relative URL
    urlStrStart = strchr(urlStrStart + sizeof("http://"), '/');
    if (urlStrStart == NULL)
        goto _error_parse_http_url;
    httpHeader_p->url = urlStrStart;
    // fprintf(outFp->msg_fp, "客户请求的相对URL：\"%s\"\n", urlStrStart);
    // fflush(outFp->msg_fp);
    *urlStrEnd = ' ';
    return 0;
}

static int parseHttpVersion (struct HttpHeader *httpHeader_p, char *req_p, struct OutputFp *outFp)
{
    // parse HTTP version
    char *httpVersion = strstr(req_p, "HTTP/");
    if (httpVersion == NULL) {
        fprintf(outFp->err_fp, "解析HTTP请求头信息失败：内容并非HTTP协议头（无法解析HTTP版本）\n");
        fflush(outFp->err_fp);
        return -1;
    }
    *(uint64_t *)httpHeader_p->httpVersion = 0;
    strcpy(httpHeader_p->httpVersion, httpVersion + sizeof("HTTP/") - 1);
    return 0;
}




int parseHttpHeader (char *reqBuff, size_t reqBuffSize, struct HttpHeader *httpHeader_p, struct OutputFp *outFp)
{
    int retStat = 0;        // the returned status of function calls
    char *startOfLine = reqBuff,
         *endOfLine,
         *dstPortStr;
    
    if (startOfLine == NULL) {
        fprintf(outFp->err_fp, "解析HTTP请求头信息失败：请求字符串为NULL\n");
        fflush(outFp->err_fp);
        return -1;
    }

    httpHeader_p -> requestStr = (char *)reqBuff;
    reqBuff[reqBuffSize - 1] = '\0';        // prevents overflow
    
    endOfLine = strchr(startOfLine, '\r');
    if (endOfLine == NULL) {
        fprintf(outFp->err_fp, "解析HTTP请求头信息失败：内容并非HTTP协议头\n");
        fflush(outFp->err_fp);
        return -1;
    } 

    *endOfLine = '\0';
    retStat = parseHttpMethod(httpHeader_p, startOfLine, outFp);
    if (httpHeader_p->method == METHOD_CONNECT) {
        *endOfLine = '\r';
        goto _parse_http_header_fields;
    }

    retStat |= parseHttpUrl(httpHeader_p, startOfLine, outFp);
    retStat |= parseHttpVersion(httpHeader_p, startOfLine, outFp);
    if (retStat < 0) {
        return retStat; 
    }
    *endOfLine = '\r';

    _parse_http_header_fields:
    while (1) {

        startOfLine = endOfLine + 2;
        endOfLine = strchr(startOfLine, '\r');
        if (startOfLine == endOfLine)
            break;
        
        *endOfLine = '\0';

        switch (*startOfLine) {
            case 'H':   // Host: ......
                if (strstr(startOfLine + 1, "ost: ")) {
                    httpHeader_p->host = startOfLine + sizeof("Host: ") - 1;
                    dstPortStr = strchr(httpHeader_p->host, ':');
                    if ((dstPortStr == NULL) & (httpHeader_p->method == METHOD_CONNECT)) {
                        httpHeader_p->dstPort = HTTPS_PORT;
                    } else if (dstPortStr == NULL) {    // methods other than CONNECT
                        httpHeader_p->dstPort = HTTP_PORT;
                    } else {
                        for (char *i = ++dstPortStr; i < endOfLine; i++) {
                            if ((*i > '9') | (*i < '0')) {
                                fprintf(outFp->err_fp, "解析HTTP请求头信息失败：内容并非HTTP协议头（无法解析目标端口）\n");
                                fflush(outFp->err_fp);
                                return -1;
                            }
                        }
                        httpHeader_p->dstPort = atoi(dstPortStr);
                    }
                    // fprintf(outFp->msg_fp, "host: \"%s\"\tport: %u\n", httpHeader_p->host, httpHeader_p->dstPort);
                    // fflush(outFp->msg_fp);
                }
                break;

            case 'C':   // Cookie: ......
                if ((strstr(startOfLine + 1, "ookie: ") != NULL)){
                    httpHeader_p->cookie = startOfLine + sizeof("Cookie: ") - 1;
                    // fprintf(outFp->msg_fp, "cookie: \"%s\"\n", httpHeader_p->cookie);
                    // fflush(outFp->msg_fp);
                }
                break;
            
            case 'P':   //Proxy-Connection: ......
                if (!strstr(startOfLine + 1, "roxy-Connection: ")) {
                    break;
                } else if (strstr(startOfLine + sizeof("Proxy-Connection: ") - 1, "close") || \
                        strstr(startOfLine + sizeof("Proxy-Connection: ") - 1, "Close")) {
                    httpHeader_p->connection = CONNECTION_CLOSE;
                    // fprintf(outFp->msg_fp, "Proxy-Connection: \"close\"\n");
                } else if (strstr(startOfLine + sizeof("Proxy-Connection: ") - 1, "keep-alive") || \
                        strstr(startOfLine + sizeof("Proxy-Connection: ") - 1, "Keep-Alive")) {
                    httpHeader_p->connection = CONNECTION_KEEP_ALIVE;
                    // fprintf(outFp->msg_fp, "Proxy-Connection: \"keep-alive\"\n");
                } else {
                    httpHeader_p->connection = CONNECTION_UNKNOWN;
                    fprintf(outFp->err_fp, "解析HTTP请求头信息失败：非法\"Proxy-Connection\"值\n");
                    fflush(outFp->err_fp);
                    return -1;
                }
                // fflush(outFp->msg_fp);
                break;
            
        }

        *endOfLine = '\r';
    }
    
    return 0;
}

int getHttpHeaderFieldStr 
(
    char *PARAM_OUT buff, 
    size_t PARAM_OUT buffSize, 
    char *PARAM_IN httpHeaderField_p,
    struct OutputFp * PARAM_IN outFp
)
{
    char *endOfLine;
    if ((httpHeaderField_p == NULL) | (buff == NULL)) {
        fprintf(outFp->err_fp, "getHttpHeaderFieldStr错误：传入值为NULL\n");
        fflush(outFp->err_fp);
        return -1;
    }
    
    endOfLine = strchr(httpHeaderField_p, '\r');
    if (endOfLine == NULL) {
        fprintf(outFp->err_fp, "getHttpHeaderFieldStr错误：传入非法字符串\"%s\"\n", httpHeaderField_p);
        fflush(outFp->err_fp);
        return -2;
    } else if ((ssize_t)buffSize <= ((ssize_t)endOfLine - (ssize_t)httpHeaderField_p)) {
        fprintf(outFp->err_fp, "getHttpHeaderFieldStr错误：空间不足\n");
        fflush(outFp->err_fp);
        return -3;
    }

    *endOfLine = '\0';
    strcpy(buff, httpHeaderField_p);
    *endOfLine = '\r';
    return 0;
}




int removeHttpHeaderElement(char *buff, char *reqBuff, char *element)
{
    char *elementInBuff;
    char *elementInReq;
    char *endOfElement;
    // fprintf(outFp->msg_fp, "\n\nbuff: \"%s\"\nreqBuff: \"%s\"\nelement: \"%s\"\n", buff, reqBuff, element);
    // fflush(outFp->msg_fp);
    elementInBuff = strstr(buff, element);
    if ((elementInBuff != NULL) && (*(elementInBuff-1) == '\n')) {
        elementInReq = strstr(reqBuff, element);
        endOfElement = strchr(elementInReq, '\n');
        if ((endOfElement[1] == '\r' & endOfElement[2] == '\n') | (endOfElement[1] == '\0')) {            
            *(uint32_t *)elementInBuff = *(uint32_t *)"\r\n\0";
        } else {
            strcpy(elementInBuff, endOfElement+1);
        }
    }
    return (int)elementInBuff;
}






// int main (void) {   // for testing parseHttpHeader() function
//     struct HttpHeader httpHeader;
//     char reqBuff[] = 
//         "GET http://ocsp.pki.goog/gts1c3/MFcwVaADAgEAME4wTDBKMAkGBSsOAwIaBQAEFMcueYrd%2F2E0s7rtR0K4u8bAJAdjBBSKdH%2Bvhc3ulc09nNDiRhTzcTUdJwIRAJzZog%2F%2B3SsKEgAAAAADbws%3D HTTP/1.1\r\n"
//         "Host: ocsp.pki.goog\r\n"
//         "X-Apple-Request-UUID: 6A43ABF1-BC5B-4E4F-887C-35E405EF1D28\r\n"
//         "Proxy-Connection: keep-alive\r\n"
//         "Accept: */*\r\n"
//         "User-Agent: com.apple.trustd/2.1\r\n"
//         "Accept-Language: zh-CN,zh-Hans;q=0.9\r\n"
//         "Accept-Encoding: gzip, deflate\r\n"
//         "Connection: keep-alive\r\n\r\n";
//     parseHttpHeader(reqBuff, sizeof(reqBuff), &httpHeader);
//     return 0;
// }
// int main (void) {   // for testing getHttpHeaderFieldStr() function
//     char reqBuff[] = 
//         "X-Apple-Request-UUID: 6A43ABF1-BC5B-4E4F-887C-35E405EF1D28\r\n"
//         "Proxy-Connection: keep-alive\r\n"
//         "Accept: */*\r\n"
//         "Connection: keep-alive\r\n\r\n";
//     char buff[128];
//     getHttpHeaderFieldStr(buff, sizeof(buff), reqBuff);
//     printf("\"%s\"\n", buff);
//     return 0;
// }

