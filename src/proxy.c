#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "proxy.h"

// clang -o $(pwd)/proxy -Wall -Wextra $(pwd)/proxy.c $(pwd)/ClientHandle.c $(pwd)/httpHeader.c
// clang -o $(pwd)/proxy -Wall -Wextra -Werror $(pwd)/proxy.c $(pwd)/ClientHandle.c $(pwd)/httpHeader.c
// clang -o proxy proxy.c ClientHandle.c

static int initSock (void);
int initBadRequestResponse (void);
int initNotFoundResponse (void);

int proxySockfd = -1;
struct sockaddr_in proxySockAddr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY };

pthread_t pthread_id[MAX_THREAD];
size_t pthread_index = 0;


char *notFoundResponse = NULL;
size_t notFoundResponseLength;
char *badRequestResponse = NULL;
size_t badRequestResponseLength;


int main (void) 
{
    int clientSockfd = -1;
    int status = 0;
    struct sockaddr clientSockAddr = {0};
    socklen_t clientSocklen = 0;
    struct ClientHandleArg clientHandleArg = {0};
    

    fprintf(stdout, "代理服务器正在启动\n");
    status = initNotFoundResponse() | initBadRequestResponse();
    if (status < 0) {
        goto _error_return;
    }

    status = initSock();
    if (status < 0) {
        goto _error_return;
    }

    fprintf(stdout, "接受请求...\n");
    fflush(stdout);
    
    do {
        clientSockfd = accept(proxySockfd, &clientSockAddr, &clientSocklen);
        clientHandleArg.clientSockfd = clientSockfd;
        clientHandleArg.clientSockAddr = clientSockAddr;
        clientHandleArg.clientSocklen = clientSocklen;
        pthread_create(&pthread_id[pthread_index++], NULL, (void *)clientHandle, &clientHandleArg);
        if (pthread_index == sizeof(pthread_id))
        // clientHandle(&clientHandleArg);
        while (clientHandleArg.clientSocklen); // wait for the thread handle to copy information

    } while (1);
    
    
    free(notFoundResponse);
    free(badRequestResponse);
    return 0;

    _error_return:
    free(notFoundResponse);
    free(badRequestResponse);
    return -1;
}



static int initSock (void)
{
    proxySockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySockfd < 0){
		errmsgout(stderr, "创建代理服务器套接字失败");
		return -1;
	}
    if (setsockopt(proxySockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        errmsgout(stderr, "setsockopt(SO_REUSEADDR) failed");
    printf("成功创建代理服务器套接字\n");

    proxySockAddr.sin_port = htons(SERVICE_PORT);
    if (bind(proxySockfd, (struct sockaddr *)&proxySockAddr, sizeof(proxySockAddr))) {
        errmsgout(stderr, "绑定代理服务器套接字失败");

        error_close_return:
        if (close(proxySockfd) < 0) 
            errmsgout(stderr, "关闭代理服务器套接字失败");
        return -1;
    }
    printf("成功绑定代理服务器套接字\n");

    if (listen(proxySockfd, MAX_CONN) < 0) {
        printf("监听代理服务器端口%d", SERVICE_PORT);
        errmsgout(stderr, "失败");
        goto error_close_return;
    }
    printf("正在监听代理服务器端口%d\n", SERVICE_PORT);

    return 0;
}


int initBadRequestResponse (void)
{
    char html[BADREQUEST_RESPONSE_BUFF_SIZE];
    badRequestResponse = malloc(BADREQUEST_RESPONSE_BUFF_SIZE);
    FILE *ffp = fopen(BADREQUEST_HTML_FILE, "rt");
    if ((badRequestResponse == NULL) | (ffp == NULL)) {
        fprintf(stderr, "初始化409 Bad Request响应时错误\n");
        return -1;
    }
    fread(html, sizeof(html), 1, ffp);
    fclose(ffp);
    sprintf(badRequestResponse,
        "HTTP/1.1 400 Bad Request\r\n"
        "Connection: Close\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zd\r\n"
        "\r\n%s",
        strlen(html),
        html
    );
    badRequestResponseLength = strlen(badRequestResponse);
    return 0;
}

int initNotFoundResponse (void)
{
    char html[NOTFOUND_RESPONSE_BUFF_SIZE];
    notFoundResponse = malloc(NOTFOUND_RESPONSE_BUFF_SIZE);
    FILE *ffp = fopen(NOTFOUND_HTML_FILE, "rt");
    if ((notFoundResponse == NULL) | (ffp == NULL)) {
        fprintf(stderr, "初始化404 Not Found响应时错误\n");
        return -1;
    }
    fread(html, sizeof(html), 1, ffp);
    fclose(ffp);
    sprintf(notFoundResponse,
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: Close\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zd\r\n"
        "\r\n%s",
        strlen(html),
        html
    );
    notFoundResponseLength = strlen(notFoundResponse);
    return 0;
}

