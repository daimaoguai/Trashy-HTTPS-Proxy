#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include "proxy.h"
#include "httpHeader.h"


static int connectServer (int *sockfp, char *host, int port, struct OutputFp *outFp);
static int httpRequestHandle (int clientSockfd, struct HttpHeader *httpHeader_p, char *buff, char *hostname, struct OutputFp *outFp);
static int getRequest404Handle (int clientSockfd, struct OutputFp *outFp);
static int connectRequestHandle (int clientSockfd, struct HttpHeader *httpHeader_p, char *buff, char *hostname, struct OutputFp *outFp);
static int proxyHandle (int clientSockfd, int serverSockfd, char *buff, struct OutputFp *outFp);
static void clientHandleCleanup (void *buff, struct OutputFp *outFp);
int clientHandle (struct ClientHandleArg *arg);



static void clientHandleCleanup (void *buff, struct OutputFp *outFp) 
{
    fprintf(outFp->msg_fp, "\n\ncleaning up...\n");
    fflush(outFp->msg_fp);

    #ifdef OUTPUT_MSG_TO_FILE
    if (fclose(outFp->msg_fp)) {
        errmsgout(outFp->err_fp, "关闭文件描述符失败");
    }
    if (fflush(outFp->err_fp) | fclose(outFp->err_fp)) {
        errmsgout(stderr, "关闭文件描述符失败");
    }    
    #endif
    free(buff);
}



int clientHandle (struct ClientHandleArg *arg)
{
    int status = 0;
    int clientSockfd = arg->clientSockfd;
    ssize_t recvSize = 0;
    char *recvBuffer;
    char *buff = NULL;
    struct OutputFp out_fp;

    struct sockaddr clientSockAddr = arg->clientSockAddr;
    // socklen_t clientSocklen = arg->clientSocklen;        // Unused variable
    struct HttpHeader httpHeader = {0};
    char clientIpStr[IPSTRING_MAX_LENGTH];
    char hostname[HOSTNAME_MAX_LENGTH];
    
    
    
    
    arg->clientSocklen = 0;     // tell caller function that information are copied

    if (buff == NULL){
        buff = malloc(BUFF_SIZE + RECVBUFF_SIZE);
        recvBuffer = buff + BUFF_SIZE;
        if (buff == NULL){
            errmsgout(stderr, "malloc failed");
            return -1;
        }
    }


    #ifdef OUTPUT_MSG_TO_FILE
    sprintf(buff, MSGOUT_FILE"%d.txt", clientSockfd);
    out_fp.msg_fp = fopen(buff, "wt");
    if (out_fp.msg_fp == NULL) {
        errmsgout(stderr, "fopen failed");
        return -1;
    }
    #else
    out_fp.msg_fp = stdout;
    #endif
    out_fp.err_fp = out_fp.msg_fp;
    

    if (inet_ntop(AF_INET, &(((struct sockaddr_in *)&clientSockAddr)->sin_addr), clientIpStr, sizeof(clientIpStr)) == NULL) {
        errmsgout(out_fp.err_fp, "无法解析客户IP地址");
        clientHandleCleanup(buff, &out_fp);
        return -1;
    }

    fprintf(out_fp.msg_fp, "新客户[IP:\"%s\"\t套接字FP:\"%d\"]\n", clientIpStr, clientSockfd);


    // Start receive request from client
    recvSize = recv(clientSockfd, (void *)recvBuffer, BUFF_SIZE, 0);
    httpHeader.recvSize = recvSize;

    if (recvSize == 0) {
        // client closed connection
        fprintf(out_fp.msg_fp, "客户[\"%s\"]关闭了链接\n", clientIpStr);
        fflush(out_fp.msg_fp);
        clientHandleCleanup(buff, &out_fp);
        return 0;

    } else if (recvSize < 0) {
        // fprintf(outFp->err_fp, "接收客户[\"%s\"]");
        errmsgout(out_fp.err_fp, "接收客户[\"%s\"]的信息错误", clientIpStr);
        status = recvSize;
        error_close_return:
        if (close(clientSockfd))
            errmsgout(out_fp.err_fp, "关闭客户套接字失败");
        clientHandleCleanup(buff, &out_fp);
        return status;
    }


    if (parseHttpHeader(recvBuffer, RECVBUFF_SIZE, &httpHeader, &out_fp) < 0)
        goto error_close_return;
    
    fprintf(out_fp.msg_fp, "\n\nrequest from client:*****************************\n%s\n*****************************\n\n\n", recvBuffer);
    fflush(out_fp.msg_fp);

    getHttpHeaderFieldStr(hostname, sizeof(hostname), httpHeader.host, &out_fp);
    switch (httpHeader.method) {

        case METHOD_GET:
        case METHOD_POST:
        case METHOD_HEAD:
        case METHOD_PUT:
        case METHOD_DELETE:
        case METHOD_OPTIONS:
        case METHOD_TRACE:
        case METHOD_PATCH:
            getHttpHeaderFieldStr(buff, BUFF_SIZE, httpHeader.requestStr, &out_fp);  // impossible to throw error
            char *methodEnd = strchr(buff, ' ');
            *methodEnd = '\0';
            fprintf(out_fp.msg_fp, "客户[\"%s\", %d]请求%s：\"%s\"\n", clientIpStr, clientSockfd, buff, hostname);
            fflush(out_fp.msg_fp);
            *methodEnd = ' ';
            status = httpRequestHandle(clientSockfd, &httpHeader, buff, hostname, &out_fp);
            if (status < 0){
                errmsgout(out_fp.err_fp, "Error handling request");
                goto error_close_return;
            }
            break;
        
        case METHOD_CONNECT:
            fprintf(out_fp.msg_fp, "客户[\"%s\", %d]请求CONNECT：\"%s\"\n", clientIpStr, clientSockfd, hostname);
            fflush(out_fp.msg_fp);
            status = connectRequestHandle(clientSockfd, &httpHeader, buff, hostname, &out_fp);
            if (status < 0)
                goto error_close_return;
            break;
        
        case METHOD_UNKNOWN:
        default:
            fprintf(out_fp.err_fp, "客户[\"%s\", %d]请求非法方法：\"%s\"\n", clientIpStr, clientSockfd, recvBuffer);
            fflush(out_fp.err_fp);
            status = -1;
            goto error_close_return;
    }

    status = close(clientSockfd);
    if (status < 0) {
        errmsgout(out_fp.err_fp, "关闭客户套接字失败");
        clientHandleCleanup(buff, &out_fp);
        return status;
    }
    fprintf(out_fp.msg_fp, "与客户[%d]的连接已断开\n", clientSockfd);
    fflush(out_fp.msg_fp);
    clientHandleCleanup(buff, &out_fp);
    return 0;

}




static int connectServer(int *sockfp, char *host, int port, struct OutputFp *outFp)
{
    int status = 0;
    char *endOfHostname_p = strchr(host, ':');
    struct hostent *hostent_p = NULL;
    struct sockaddr_in serverAddr;
    struct in_addr inaddr;
    
    
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (endOfHostname_p)
        *endOfHostname_p = '\0';
    hostent_p = gethostbyname(host);
    if (endOfHostname_p)
        *endOfHostname_p = ':';

    if (hostent_p == NULL) {
        errmsgout(outFp->err_fp, "解析域名[%s]失败（返回值为NULL）\n", host);
        return -1;
    }

    inaddr = *((struct in_addr*)*(hostent_p->h_addr_list));//主机的ip地址


    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(inaddr));
    *sockfp = socket(AF_INET, SOCK_STREAM, 0);
    if (*sockfp < 0) {
        errmsgout(outFp->err_fp, "创建用于连接服务器的代理服务器套接字失败");
        return *sockfp;
    }

    fprintf(outFp->msg_fp, "正在连接至服务器\n");
    fflush(outFp->msg_fp);

    status = connect(*sockfp, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (status < 0){
        errmsgout(outFp->err_fp, "连接服务器失败");
        if (close(*sockfp) < 0) {
            errmsgout(outFp->err_fp, "关闭用于连接服务器的套接字失败");
        }
		return status;
	}
    fprintf(outFp->msg_fp, "成功连接至[%s]\n", host);
    fflush(outFp->msg_fp);
    return 0;
}



static int httpRequestHandle(int clientSockfd, struct HttpHeader *httpHeader_p, char *buff, char *hostname, struct OutputFp *outFp)
{
    int serverSockfd = -1;
    int status = 0;
    ssize_t sentSize = 0;

    if (strstr(hostname, "127.0.0.1") || strstr(hostname, "localhost")){
        return getRequest404Handle(clientSockfd, outFp);
    } else if (strstr(hostname, PRIVATE_IP) || strstr(hostname, PUBLIC_IP)){
        return getRequest404Handle(clientSockfd, outFp);
    }
    

    status = connectServer(&serverSockfd, hostname, httpHeader_p->dstPort, outFp);
    if (status < 0) {
        return status;
    }


    strcpy(buff, httpHeader_p->requestStr);

    // if parseHttpHeader() returned success, it is impossible for strchr() to return NULL
    strcpy(strchr(buff, ' ') + 1, httpHeader_p->url);   // change absolute url to relative url

    char *proxyConnection = strstr(buff, "Proxy-Connection");
    if (proxyConnection != NULL) {
        strcpy(proxyConnection, strstr(httpHeader_p->requestStr, "Proxy-Connection") + sizeof("proxy"));
    }
    
    removeHttpHeaderElement(buff, httpHeader_p->requestStr, "Proxy-Connection");
    removeHttpHeaderElement(buff, httpHeader_p->requestStr, "Cache-Control");
    
    fprintf(outFp->msg_fp, "\n\nRequest to server:\n\"%s\"\n", buff);
    fflush(outFp->msg_fp);
    sentSize = send(serverSockfd, buff, strlen(buff), 0);
    if (sentSize < 0) {
        errmsgout(outFp->err_fp, "发送请求至服务器[%s]错误", hostname);
        goto error_return;
    } else if (sentSize == 0) {
        fprintf(outFp->msg_fp, "服务器[%s]关闭了链接\n", hostname);
        fflush(outFp->msg_fp);
        return 0;
    }



    status = proxyHandle(clientSockfd, serverSockfd, buff, outFp);
    if (proxyHandle < 0) {
        goto error_return;
    }

    status = close(serverSockfd);
    if (status < 0) {
        errmsgout(outFp->err_fp, "Error closing server socket");
        return status;
    } else {
        return 0;
    }

    error_return:
    status = close(serverSockfd);
    if (status < 0) {
        errmsgout(outFp->err_fp, "Error closing server socket");
    }
    return -1;

}



#define CONNECT_RESPONSE            "HTTP/1.1 200 Connection Established\r\n"
#define CONNECTION_RSP_KEEPALIVE    "Proxy-Connection: keep-alive\r\n\r\n"
#define CONNECTION_RSP_CLOSE        "Proxy-Connection: close\r\n\r\n"

#define OUTPUT_RESPONSE_STRING      "\nResponded client:*****************************\n%s\n*****************************\n\n\n"

static int connectRequestHandle(int clientSockfd, struct HttpHeader *httpHeader_p, char *buff, char *hostname, struct OutputFp *outFp)
{
    int serverSockfd = -1, 
        status = 0;
    ssize_t sentSize;

    status = connectServer(&serverSockfd, hostname, httpHeader_p->dstPort, outFp);
    if (status < 0) {
        return status;
    }


    if (httpHeader_p->connection == CONNECTION_KEEP_ALIVE) {
        sentSize = send(clientSockfd, 
                        CONNECT_RESPONSE CONNECTION_RSP_KEEPALIVE, 
                        sizeof(CONNECT_RESPONSE CONNECTION_RSP_KEEPALIVE) - 1, 
                        0);
        fprintf(outFp->msg_fp, OUTPUT_RESPONSE_STRING, CONNECT_RESPONSE CONNECTION_RSP_KEEPALIVE);
    } else {
        sentSize = send(clientSockfd, 
                        CONNECT_RESPONSE CONNECTION_RSP_CLOSE, 
                        sizeof(CONNECT_RESPONSE CONNECTION_RSP_CLOSE) - 1, 
                        0);
        fprintf(outFp->msg_fp, OUTPUT_RESPONSE_STRING, CONNECT_RESPONSE CONNECTION_RSP_CLOSE);
    } 
    
    if (sentSize < 0) {
        errmsgout(outFp->err_fp, "发送数据至客户错误");
        return -1;
    } else if (sentSize == 0) {
        fprintf(outFp->err_fp, "客户关闭了链接\n");
        fflush(outFp->err_fp);
        return 0;
    }


    status = proxyHandle(clientSockfd, serverSockfd, buff, outFp);
    if (status < 0) {
        goto recv_send_error;
    }
    
    status = close(serverSockfd);
    if (status < 0) {
        fprintf(outFp->err_fp, "关闭连接服务器[%s]的套接字: %s\n", hostname, strerror(errno));
        fflush(outFp->err_fp);
        return status;
    } else {
        return 0;
    }

    recv_send_error:
    status = close(serverSockfd);
    if (status < 0) {
        fprintf(outFp->err_fp, "关闭连接服务器[%s]的套接字: %s\n", hostname, strerror(errno));
        fflush(outFp->err_fp);
        return status;
    }
    return -1;

}

int getRequest400Handle(int clientSockfd, struct OutputFp *outFp)
{
    fprintf(outFp->msg_fp, OUTPUT_RESPONSE_STRING, badRequestResponse);
    fflush(outFp->msg_fp);
    return send(clientSockfd, badRequestResponse, badRequestResponseLength, 0);
}


static int getRequest404Handle(int clientSockfd, struct OutputFp *outFp)
{
    fprintf(outFp->msg_fp, OUTPUT_RESPONSE_STRING, notFoundResponse);
    fflush(outFp->msg_fp);
    return send(clientSockfd, notFoundResponse, notFoundResponseLength, 0);
}


// loop to transfer data between client and destinated server
static int proxyHandle (int clientSockfd, int serverSockfd, char *buff, struct OutputFp *outFp)
{
    ssize_t dataSize = 0;
    fd_set rdfdset, rdfdset2;
    FD_ZERO(&rdfdset);
    FD_SET(clientSockfd, &rdfdset);
    FD_SET(serverSockfd, &rdfdset);
    rdfdset2 = rdfdset;
        
    while (1) {
        rdfdset = rdfdset2;
        if (select(FD_SETSIZE, &rdfdset, NULL, NULL, NULL) < 0) {
            errmsgout(outFp->err_fp, "select函数调用失败");
            return -1;
        }

        if (FD_ISSET(clientSockfd, &rdfdset)){
            dataSize = read(clientSockfd, buff, BUFF_SIZE);
            if (dataSize > 0) {
                fprintf(outFp->msg_fp, "从客户读取了%zu字节\n",  dataSize);
                fflush(outFp->msg_fp);
                dataSize = write(serverSockfd, buff, dataSize);
                if (dataSize < 0) {
                    errmsgout(outFp->err_fp, "发送数据至服务器错误");
                    return -1;
                } else if (dataSize == 0) {
                    fprintf(outFp->msg_fp, "服务器关闭了链接\n");
                    fflush(outFp->msg_fp);
                    return 0;
                } else {
                    fprintf(outFp->msg_fp, "向服务器发送了%zu字节\n", dataSize);
                    fflush(outFp->msg_fp);
                }
            } else if (dataSize == 0) {
                fprintf(outFp->msg_fp, "客户关闭了链接\n");
                fflush(outFp->msg_fp);
                break;
            } else {
                errmsgout(outFp->err_fp, "从客户读取数据错误");
                return -1;
            }
        } else if (FD_ISSET(serverSockfd, &rdfdset)){
            dataSize = read(serverSockfd, buff, BUFF_SIZE);
            if (dataSize > 0) {
                fprintf(outFp->msg_fp, "从服务器读取了%zu字节\n", dataSize);
                fflush(outFp->msg_fp);
                dataSize = write(clientSockfd, buff, dataSize);
                if (dataSize < 0) {
                    errmsgout(outFp->err_fp, "发送数据至客户错误");
                    break;
                } else if (dataSize == 0) {
                    fprintf(outFp->msg_fp, "客户关闭了链接\n");
                    fflush(outFp->msg_fp);
                    break;
                } else {
                    fprintf(outFp->msg_fp, "向客户发送了%zu字节\n", dataSize);
                    fflush(outFp->msg_fp);
                }
            } else if (dataSize == 0) {
                fprintf(outFp->msg_fp, "服务器关闭了链接\n");
                fflush(outFp->msg_fp);
                return 0;
            } else {
                errmsgout(outFp->err_fp, "从服务器读取数据错误");
                return -1;
            }
            
        }
        
    }
    return 0;
}