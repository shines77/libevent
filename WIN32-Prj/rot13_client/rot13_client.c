#ifndef WIN32
/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>
#endif

#include <event2/event.h>

#include <util-internal.h>

#include <assert.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <tchar.h>

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

struct fd_state {
    char buffer[MAX_LINE];
    size_t buffer_used;

    size_t n_written;
    size_t write_upto;

    struct event *read_event;
    struct event *write_event;
};

struct fd_state *
alloc_fd_state(struct event_base *base, evutil_socket_t fd)
{
    struct fd_state *state = malloc(sizeof(struct fd_state));
    if (!state)
        return NULL;
    state->read_event = event_new(base, fd, EV_READ|EV_PERSIST, do_read, state);
    if (!state->read_event) {
        free(state);
        return NULL;
    }
    else {
        printf("event_new(state->read_event): 0x%08X\r\n", state->read_event);
    }
    state->write_event =
        event_new(base, fd, EV_WRITE|EV_PERSIST, do_write, state);

    if (!state->write_event) {
        event_free(state->read_event);
        free(state);
        return NULL;
    }
    else {
        printf("event_new(state->write_event): 0x%08X\r\n", state->write_event);
    }

    state->buffer_used = state->n_written = state->write_upto = 0;

    assert(state->write_event);
    return state;
}

void
free_fd_state(struct fd_state *state)
{
    event_free(state->read_event);
    event_free(state->write_event);
    free(state);
}

void
do_read(evutil_socket_t fd, short events, void *arg)
{
    struct fd_state *state = arg;
    char buf[1024];
    int i;
    ev_ssize_t result;

    printf("do_read(): fd = %d\r\n", fd);
    while (1) {
        assert(state->write_event);
        result = recv(fd, buf, sizeof(buf), 0);
        printf("recv(): result = %d\r\n", result);
        if (result <= 0)
            break;

        for (i=0; i < result; ++i)  {
            if (state->buffer_used < sizeof(state->buffer))
                state->buffer[state->buffer_used++] = rot13_char(buf[i]);
            if (buf[i] == '\n') {
                //assert(state->write_event);
                //event_add(state->write_event, NULL);
                state->write_upto = state->buffer_used;
                buf[state->buffer_used] = '\0';
                printf("\r\n");
                printf("bufsize = %d, buf = %s\r\n", state->buffer_used, buf);
            }
        }
    }

    if (result == 0) {
        free_fd_state(state);
    } else if (result < 0) {
        //if (errno == EAGAIN) // XXXX use evutil macro
        //    return;
        int err = evutil_socket_geterror(fd);
        if (EVUTIL_ERR_RW_RETRIABLE(err)) {
            // WSAEWOULDBLOCK or WSAEINTR
            printf("event_del(state->read_event);\r\n");
            event_del(state->read_event);
            return;
        }
        else {
            perror("recv");
            free_fd_state(state);
        }
    }
}

void
do_write(evutil_socket_t fd, short events, void *arg)
{
    struct fd_state *state = arg;
    char buf[512];
    int n_written = 0, write_upto = sizeof(buf);
    int buffer_used = 0;

    printf("do_write(): fd = %d\r\n", fd);

    memset(buf, 0, write_upto);
    strcpy(buf, "Hello World!\r\n");

    printf("\r\n");
    printf("bufsize = %d, buf = %s\r\n", state->buffer_used, buf);

    write_upto = strlen(buf);

    while (n_written < write_upto) {
        ev_ssize_t result = send(fd, buf + n_written,
                              write_upto - n_written, 0);
        printf("send(): result = %d\r\n", result);
        if (result < 0) {
            //if (errno == EAGAIN) // XXX use evutil macro
            //    return;
		    int err = evutil_socket_geterror(fd);
            if (EVUTIL_ERR_RW_RETRIABLE(err)) {
                // WSAEWOULDBLOCK or WSAEINTR
                break;
            }
            else {
                printf("event_del(state->write_event);\r\n");
                event_del(state->write_event);
                return;
            }
        }
        assert(result != 0);

        n_written += result;
    }

    if (n_written == buffer_used)
        n_written = write_upto = buffer_used = 1;

    printf("event_del(state->write_event);\r\n");
    event_del(state->write_event);
}

void
run(void)
{
    evutil_socket_t sender;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *write_event = NULL;
    struct event *read_event = NULL;
    struct fd_state *state;
    int result;

    base = event_base_new();
    if (!base)
        return; /*XXXerr*/

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons(40713);

    sender = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    evutil_make_socket_nonblocking(sender);

#ifndef WIN32
    {
        int one = 1;
        setsockopt(sender, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif

    /*
    if (bind(sender, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return;
    }

    sender_event = event_new(base, sender, EV_READ|EV_PERSIST, do_read, (void*)base);
    /-*XXX check it *-/
    event_add(sender_event, NULL);
    //*/

    result = connect(sender, (struct sockaddr*)&sin, sizeof(sin));
    if (result < 0 || result == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            printf("WSAGetLastError() = WSAEWOULDBLOCK\r\n");
            Sleep(1000);
            printf("Press any key to continue...\r\n");
            //system("pause");
        }
        else {
            printf("connect error!\r\n");
            printf("WSAGetLastError() = 0x%08X\r\n", WSAGetLastError());
            system("pause");
            perror("connect");
            return;
        }
    }

    //printf("Press any key to continue...\r\n");
    //system("pause");

    //write_event = event_new(base, sender, EV_WRITE|EV_PERSIST, do_write, (void*)base);
    //read_event  = event_new(base, sender, EV_READ |EV_PERSIST, do_read,  (void*)base);

    state = alloc_fd_state(base, sender);

    /*
    if (write_event) {
        printf("event_add(write_event, NULL);\r\n");
        event_add(write_event, NULL);
    }
    //*/

    if (state && state->write_event) {
        printf("event_add(state->write_event, NULL);\r\n");
        event_add(state->write_event, NULL);
    }

    if (state && state->read_event) {
        printf("event_add(state->read_event, NULL);\r\n");
        event_add(state->read_event, NULL);
    }

    printf("event_base_dispatch(base);\r\n");
    event_base_dispatch(base);

    /*
    if (write_event)
        event_free(write_event);
    if (read_event)
        event_free(read_event);
    //*/

    if (state)
        free_fd_state(state);
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

    system("pause");
    return 0;
}
