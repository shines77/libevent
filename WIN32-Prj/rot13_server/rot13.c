#ifndef WIN32
/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <util-internal.h>

#include <assert.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define MAX_LINE 16384

void do_read(evutil_socket_t fd, short events, void *arg);
void do_write(evutil_socket_t fd, short events, void *arg);

char
rot13_char(char c)
{
    /* We don't want to use isalpha here; setting the locale would change
     * which characters are considered alphabetical. */
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

void
writecb(struct bufferevent *bev, void *ctx)
{
    /*
    struct evbuffer *input, *output;
    char *line;
    size_t n;
    int i;
    printf("writecb(): enter: bev = 0x%08X, ctx = 0x%08X\r\n", bev, ctx);

    input = bufferevent_get_input(bev);
    output = bufferevent_get_output(bev);

    printf("writecb(): over\r\n", bev, ctx);
    //*/
}

void
readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *input, *output;
    char *line;
    size_t n;
    int i;
    printf(">>>\r\n");
    printf("readcb(): enter: bev = 0x%08X, ctx = 0x%08X\r\n", bev, ctx);
    printf(">>>\r\n");

    input = bufferevent_get_input(bev);
    output = bufferevent_get_output(bev);

    while ((line = evbuffer_readln(input, &n, EVBUFFER_EOL_LF))) {
        printf("evbuffer_readln(): n = %d\r\n", n);
        for (i = 0; (size_t)i < n; ++i)
            line[i] = rot13_char(line[i]);
        evbuffer_add(output, line, n);
        evbuffer_add(output, "\n", 1);
        free(line);
    }

    if (evbuffer_get_length(input) >= MAX_LINE) {
        /* Too long; just process what there is and go on so that the buffer
         * doesn't grow infinitely long. */
        char buf[1024];
        printf("if (evbuffer_get_length(input) >= MAX_LINE) { }\r\n");
        while (evbuffer_get_length(input)) {
            int n = evbuffer_remove(input, buf, sizeof(buf));
            for (i = 0; i < n; ++i)
                buf[i] = rot13_char(buf[i]);
            evbuffer_add(output, buf, n);
        }
        evbuffer_add(output, "\n", 1);
    }

    printf(">>>\r\n");
    printf("readcb(): over\r\n", bev, ctx);
    printf(">>>\r\n");
}

void
errorcb(struct bufferevent *bev, short error, void *ctx)
{
    printf("errorcb(): enter: bev = 0x%08X, error = %d\r\n", bev, error);
    if (error & BEV_EVENT_EOF) {
        /* connection has been closed, do any clean up here */
        printf("errorcb(): connection has been closed.\r\n");
        /* ... */
    } else if (error & BEV_EVENT_ERROR) {
        //error = EAGAIN;
        /* check errno to see what error occurred */
        int err;
        err = evutil_socket_geterror(0);
        //err = WSAGetLastError();
        printf("errorcb(): evutil_socket_geterror() = 0x%08X\r\n", err);
        if (err == WSAECONNRESET) {
            // WSAECONNRESET
            printf("errorcb(): WSAECONNRESET - An existing connection was forcibly closed by the remote host.\r\n");
        }
        else if (err == WSAENETRESET) {
            // WSAENETRESET
            printf("errorcb(): WSAENETRESET - The connection has been broken due to keep-alive activity detecting a failure while the operation was in progress.\r\n");
        }
        else if (err == WSAECONNABORTED) {
            // WSAECONNABORTED
            printf("errorcb(): WSAECONNABORTED - An established connection was aborted by the software in your host machine.\r\n");
        }
        else if (EVUTIL_ERR_RW_RETRIABLE(err)) {
            // WSAEWOULDBLOCK or WSAEINTR
            printf("errorcb(): WSAEWOULDBLOCK or WSAEINTR.\r\n");
        }
        else {
            printf("errorcb(): check errno to see what error occurred.\r\n");
        }
        /* ... */
    } else if (error & BEV_EVENT_TIMEOUT) {
        /* must be a timeout event handle, handle it */
        printf("errorcb(): is a timeout event handle.\r\n");
        /* ... */
    }
    bufferevent_free(bev);
    printf("errorcb(): over\r\n", bev, ctx);
}

void
do_accept(evutil_socket_t listener, short event, void *arg)
{
    struct event_base *base = arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    printf("do_accept(): listener = %d, fd = %d\r\n", listener, fd);
    if (fd < 0) {
        perror("accept");
#ifndef WIN32
    } else if (fd > FD_SETSIZE) {
        //close(fd);
        evutil_closesocket(fd); // XXX replace all closes with EVUTIL_CLOSESOCKET */
#endif
    } else {
        struct bufferevent *bev;
        printf("do_accept(): fd = %d\r\n", fd);
        evutil_make_socket_nonblocking(fd);
        bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(bev, readcb, NULL, errorcb, NULL);
        bufferevent_setwatermark(bev, EV_READ, 0, MAX_LINE);
        printf("bufferevent_enable(bev, EV_READ|EV_WRITE);\r\n");
        bufferevent_enable(bev, EV_READ|EV_WRITE);
    }
}

void
run(void)
{
    evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;

    base = event_base_new();
    if (!base)
        return; /*XXXerr*/

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons(40713);

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    evutil_make_socket_nonblocking(listener);

#ifndef WIN32
    {
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return;
    }

    printf("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\r\n");
    printf("rot13_server.exe is running...\r\n");
    printf("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\r\n");
    //printf("Press any key to continue...\r\n")
    //system("pause");

    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);

    /*XXX check it */
    printf("event_add(listener_event, NULL);\r\n");
    event_add(listener_event, NULL);

    printf("event_base_dispatch(base);\r\n");
    event_base_dispatch(base);

    printf("run() loop is over.\r\n");
}

int
main(int c, char **v)
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        return 1;
    }

    /* Confirm that the WinSock DLL supports 2.2.*/
    /* Note that if the DLL supports versions greater    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 ||
        HIBYTE(wsaData.wVersion) != 2) {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        WSACleanup();
        return 1; 
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    run();

    WSACleanup();

    return 0;
}
