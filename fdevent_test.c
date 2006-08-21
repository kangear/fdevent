#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/signal.h>

#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>

#include "fdevent.h"
#include "sockets.h"

typedef struct astream astream;

struct astream 
{
    fdevent fde;
    astream *peer;

    char buf[4096];
    char *ptr;
    unsigned count;
};    

void astream_destroy(astream *as)
{
    if(as->peer) {
        if(as->peer->count == 0) {
            fdevent_remove(&as->peer->fde);
            free(as->peer);
        }
        as->peer->peer = 0;
    }

    fdevent_remove(&as->fde);
    free(as);
}

void astream_io_cb(int fd, unsigned flags, void *x)
{
    astream *as = x;

    if(flags & FDE_READ){
        if(as->peer == 0) {
            if(as->count) {
                fdevent_del(&as->fde, FDE_READ);
                return;
            } else {
                goto teardown;
            }
        }
        if(as->peer->count) {
            fdevent_del(&as->fde, FDE_READ);
        } else {
            char *ptr = as->peer->buf;
            as->peer->ptr = ptr;
            while(as->peer->count < 4096) {
                int n = read(fd, ptr, 4096 - as->peer->count);
                if(n == 0) {
                    if(as->peer->count) {
                        fdevent_add(&as->peer->fde, FDE_WRITE);
                    }
                    goto teardown;
                }
                if(n < 0) {
                    if(errno == EINTR) continue;
                    if(errno == EWOULDBLOCK) break;
                    goto teardown;
                }
                ptr += n;
                as->peer->count += n;
            }
            if(as->peer->count) {
                fdevent_add(&as->peer->fde, FDE_WRITE);
            }
        }
    }

    if(flags & FDE_WRITE){
        if(as->count == 0) {
            fdevent_del(&as->fde, FDE_WRITE);
        } else {
            while(as->count) {
                int n = write(fd, as->ptr, as->count);
                if(n < 0) {
                    if(errno == EINTR) continue;
                    if(errno == EWOULDBLOCK) break;
                    goto teardown;
                }
                as->ptr += n;
                as->count -= n;
            }
            if(as->count == 0) {
                if(as->peer) {
                    as->ptr = as->buf;
                    fdevent_add(&as->peer->fde, FDE_READ);
                } else {
                    goto teardown;
                }
            }
        }
    }

    if(flags & FDE_ERROR) {
        goto teardown;
    }
    return;

teardown:
    astream_destroy(as);
}

astream *astream_create(int fd)
{
    astream *as = (astream*) malloc(sizeof(astream));

    as->peer = 0;
    as->ptr = as->buf;
    as->count = 0;

    fdevent_install(&as->fde, fd, astream_io_cb, as);
    
    return as;    
}

void print_sock_cb(int fd, unsigned flags, void *x)
{
    char buf[1024];
    int n;
    
    for(;;) {
        n = read(fd, buf, 1024);
        if(n == 0) break;
        if(n < 0) {
            if(errno == EWOULDBLOCK) return;
            break;
        } else {
            write(1, buf, n);
        }
    }
    fdevent_destroy((fdevent*) x);
    close(fd);
}

void cnxn_sock_cb(int fd, unsigned flags, void *x)
{
    struct sockaddr addr;
    socklen_t alen;
    int s;
    int s2;
    astream *a, *b;

    alen = sizeof(addr);
    s = accept(fd, &addr, &alen);
    if(s < 0) {
        perror("accept");
        return;
    }
    
    s2 = socket_connect_to("frotz.net", 80);
    if(s2 < 0) {
        perror("connect failed");
        close(s);
        return;
    }
    
    a = astream_create(s);
    b = astream_create(s2);
    a->peer = b;
    b->peer = a;
    fdevent_set(&a->fde, FDE_READ | FDE_ERROR);
    fdevent_set(&b->fde, FDE_READ | FDE_ERROR);
}

int main(int argc, char **argv)
{
    int fd;
    fdevent *fde;

    signal(SIGPIPE, SIG_IGN);
    
    fd = socket_inaddr_any_server(3333, SOCK_STREAM);
    fde = fdevent_create(fd, cnxn_sock_cb, 0);
    fdevent_set(fde, FDE_READ);
    fde->arg = fde;

    fdevent_loop();
    return 0;
}

