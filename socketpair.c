/* http://frotznet.googlecode.com/svn/trunk/utils/watcher.c
**
** Copyright 2015, Kangear <kangear@163.net>
** Copyright 2006, Brian Swetland <swetland@frotz.net>
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

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
#include <sys/socket.h>

#include "fdevent.h"

static int transport_registration_send = -1;
static int transport_registration_recv = -1;
static fdevent transport_registration_fde;

/**
 * called by fdevent when can be read.
 * like Android App onCreate method.
 */
static void transport_registration_func(int _fd, unsigned ev, void *data)
{
    printf("%d:%s\n", __LINE__, __FUNCTION__);
    char * buf = (char*)calloc(1 , 30);
    /*****read*******/ 
    int r;
    if( (r = read(_fd, buf , 30 )) == -1){ 
            printf("Read from socket error:%s\n",strerror(errno) ); 
            exit(-1); 
    }
    // tmsg m;
    // adb_thread_t output_thread_ptr;
    // adb_thread_t input_thread_ptr;
    // int s[2];
    // atransport *t;
    // 
    // if(!(ev & FDE_READ)) {
    //     return;
    // }
    // 
    // if(transport_read_action(_fd, &m)) {
    //     fatal_errno("cannot read transport registration socket");
    // }
    // 
    // t = m.transport;
    // 
    // if(m.action == 0){
    //     D("transport: %s removing and free'ing %d\n", t->serial, t->transport_socket);
    // 
    //         /* IMPORTANT: the remove closes one half of the
    //         ** socket pair.  The close closes the other half.
    //         */
    //     fdevent_remove(&(t->transport_fde));
    //     adb_close(t->fd);
    // 
    //     adb_mutex_lock(&transport_lock);
    //     t->next->prev = t->prev;
    //     t->prev->next = t->next;
    //     adb_mutex_unlock(&transport_lock);
    // 
    //     run_transport_disconnects(t);
    // 
    //     if (t->product)
    //         free(t->product);
    //     if (t->serial)
    //         free(t->serial);
    // 
    //     memset(t,0xee,sizeof(atransport));
    //     free(t);
    // 
    //     update_transports();
    //     return;
    // }
    // 
    // /* don't create transport threads for inaccessible devices */
    // if (t->connection_state != CS_NOPERM) {
    //     /* initial references are the two threads */
    //     t->ref_count = 2;
    // 
    //     if(adb_socketpair(s)) {
    //         fatal_errno("cannot open transport socketpair");
    //     }
    // 
    //     D("transport: %s (%d,%d) starting\n", t->serial, s[0], s[1]);
    // 
    //     t->transport_socket = s[0];
    //     t->fd = s[1];
    // 
    //     fdevent_install(&(t->transport_fde),
    //                     t->transport_socket,
    //                     transport_socket_events,
    //                     t);
    // 
    //     fdevent_set(&(t->transport_fde), FDE_READ);
    // 
    //     if(adb_thread_create(&input_thread_ptr, input_thread, t)){
    //         fatal_errno("cannot create input thread");
    //     }
    // 
    //     if(adb_thread_create(&output_thread_ptr, output_thread, t)){
    //         fatal_errno("cannot create output thread");
    //     }
    // }
    // 
    //     /* put us on the master device list */
    // adb_mutex_lock(&transport_lock);
    // t->next = &transport_list;
    // t->prev = transport_list.prev;
    // t->next->prev = t;
    // t->prev->next = t;
    // adb_mutex_unlock(&transport_lock);
    // 
    // t->disconnects.next = t->disconnects.prev = &t->disconnects;
    // 
    // update_transports();
}

/**
 * init fdevent
 */
void init_transport_registration(void)
{
    int s[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, s)){
        printf("cannot open transport registration socketpair");
        return;
    }
    /* ensure only current process use these fd */
    fcntl( s[0], F_SETFD, FD_CLOEXEC );
    fcntl( s[1], F_SETFD, FD_CLOEXEC );

    transport_registration_send = s[0];
    transport_registration_recv = s[1];

    fdevent_install(&transport_registration_fde,
                    transport_registration_recv,
                    transport_registration_func,
                    0);

    fdevent_set(&transport_registration_fde, FDE_READ);
}

int main(int argc, char **argv)
{
    printf("%d:%s\n", __LINE__, __FUNCTION__);
    init_transport_registration();
    sleep(3);
    write(transport_registration_send , "string" , strlen("string")  );
    fdevent_loop();
    return 0;
}

