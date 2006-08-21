#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <errno.h>
#include <fcntl.h>

#include "fdevent.h"

int local_socket(int port)
{
    struct sockaddr_in sa;
    int opt = 1;
    int s;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if(bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        close(s);
        return -1;
    }

    if(listen(s, 5) < 0) {
        close(s);
        return -1;
    }

    return s;
}


int remote_socket(const char *host, int port)
{
    struct sockaddr_in sa;
    struct hostent *hp;
    int s;
    
    if(!(hp = gethostbyname(host))){
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_port = htons(port);
    sa.sin_family = hp->h_addrtype;
    memcpy((void*) &sa.sin_addr, (void*) hp->h_addr, hp->h_length);
    
    if((s = socket(hp->h_addrtype, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    if(connect(s, (struct sockaddr*) &sa, sizeof(sa)) != 0){
        close(s);
        return -1;
    }

    return s;
}


typedef struct astream astream;

struct astream 
{
    fdevent fde;
    astream *peer;

    char buf[4096];
    char *ptr;
    unsigned count;

    char *tag;
};    

void dump_txn(astream *as)
{
    write(1, as->tag, strlen(as->tag));
    write(1, as->buf, as->count);
    write(1, "\n", 1);
}

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
                        dump_txn(as->peer);
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
                dump_txn(as->peer);
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

int src_port = 8000;
char *dst_host = "127.0.0.1";
int dst_port = 80; 

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
    
    s2 = remote_socket(dst_host, dst_port);
    if(s2 < 0) {
        perror("connect failed");
        close(s);
        return;
    }
    
    a = astream_create(s);
    b = astream_create(s2);
    a->peer = b;
    a->tag = "<< ";
    b->peer = a;
    b->tag = ">> ";
    fdevent_set(&a->fde, FDE_READ | FDE_ERROR);
    fdevent_set(&b->fde, FDE_READ | FDE_ERROR);
}

int main(int argc, char **argv)
{
    int fd;
    fdevent *fde;

    if(argc != 4) {
        fprintf(stderr,"usage: watcher <src-port> <dst-host> <dst-port>\n");
        return 0;
    }

    src_port = atoi(argv[1]);
    dst_host = argv[2];
    dst_port = atoi(argv[3]);
    
    signal(SIGPIPE, SIG_IGN);
    
    fd = local_socket(src_port);
    fde = fdevent_create(fd, cnxn_sock_cb, 0);
    fdevent_set(fde, FDE_READ);
    fde->arg = fde;

    fdevent_loop();
    return 0;
}

