#include "socketcorpus.h"

#define _GNU_SOURCE 

#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <assert.h>
#include <poll.h>


volatile int comm_sync;
volatile int done_sending;

int 
init_vb_socketcall_info_t(vb_socketcall_info_t * info)
{

    //get call_seq_lenfrom csv 
    info->call_seq_len = info->program_info->list_length; 


    info->untimed_socket = (vb_socket_t*)malloc(sizeof(vb_socket_t));
    info->untimed_socket->fd_domain = info->program_info->connection_domain;
    info->untimed_socket->fd_type = info->program_info->connection_type;
    info->untimed_socket->fd_proto =  info->program_info->connection_proto;
    //create untimed socket now
    info->untimed_socket->fd = socket(info->untimed_socket->fd_domain,
                                        info->untimed_socket->fd_type,
                                        info->untimed_socket->fd_proto);

    //init but dont create timed socket
    info->timed_socket = (vb_socket_t*)malloc(sizeof(vb_socket_t));
    info->timed_socket->fd_domain = info->program_info->connection_domain;
    info->timed_socket->fd_type = info->program_info->connection_type;
    info->timed_socket->fd_proto = info->program_info->connection_proto;


    //set up server address info
    struct sockaddr_in* address = (struct sockaddr_in* )malloc(sizeof(struct sockaddr_in));//malloc here 
    memset(address, '0', sizeof(struct sockaddr_in)); 
    address->sin_family = info->program_info->connection_domain;
    address->sin_port = htons(info->program_info->connection_port); 
    address->sin_addr.s_addr = INADDR_ANY;
    info->server_addr = address;

    info->addrlen = sizeof(*info->server_addr); 

    //set size of timing storage arrays

    info->call_seq = (time_data_t*)malloc(info->call_seq_len*sizeof(time_data_t));
    info->call_idx=0;
    info->connection_failed=0;
    comm_sync = SERVER_TURN;
    done_sending=0;
    return 0;

}
int
deinit_vb_socketcall_info_t(vb_socketcall_info_t * info)
{

    //clean up untimed socket
    pthread_join(info->untimed_thread,NULL);
    close(info->timed_socket->fd);  
    close(info->untimed_socket->fd);
    free(info->call_seq);      
    free(info->untimed_socket);    
    free(info->timed_socket);  
    free(info->server_addr);        



    return 0;
}

static void*
untimed_client(void * ptr){
    vb_socketcall_info_t * info = (vb_socketcall_info_t*) ptr;
    int valread; 
    char *hello = "Hello from client"; 
    char buffer[1024] = {0}; 
}
static void*
untimed_server(void * ptr){
    vb_socketcall_info_t * info = (vb_socketcall_info_t*) ptr;

    struct pollfd check_on_conndf;
    int valread, connfd; 
    char buffer[1024] = {0}; 
    int opt = 1; 
    int status;
    setsockopt(info->untimed_socket->fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if(bind(info->untimed_socket->fd, (struct sockaddr *)info->server_addr, sizeof(*(info->server_addr)))<0) 
    { 
        perror("Bind Error");
        info->connection_failed=1;
        return 0;
    }  
    if (listen(info->untimed_socket->fd, info->server_backlog) < 0) 
    { 
        perror("Listen Error"); 
        info->connection_failed=1;
        comm_sync=CLIENT_TURN; 
        return 0;
    } 
    fcntl(info->untimed_socket->fd, F_SETFL, O_NONBLOCK);
    comm_sync=CLIENT_TURN; 
    while(comm_sync!=SERVER_TURN);
    if ((connfd = accept(info->untimed_socket->fd, (struct sockaddr *)info->server_addr,(socklen_t*) &(info->addrlen)))<0)
    {
        perror("Accept Error:");
        info->connection_failed=1;
        comm_sync=CLIENT_TURN; 
        return 0;
    }

    check_on_conndf.fd=connfd;
    check_on_conndf.events = POLLIN;



readagain:
    comm_sync=CLIENT_TURN; //let them send/close
    if (done_sending){return 0;}

    while(comm_sync!=SERVER_TURN); //wait for out turn
    valread = recv( connfd , buffer, 1024, 0); 
    printf("Message Recived on port: %i :%s", info->program_info->connection_port,buffer);
    goto readagain;

}

int 
begin_untimed_program(vb_socketcall_info_t * info)
{
    int status;
    if(info->timer_choice==TIME_SERVER)
    {
        //connect
        if (connect(info->untimed_socket->fd, (struct sockaddr *)info->server_addr, sizeof(struct sockaddr)) < 0) 
        { 
            perror("untimed Connection Failed \n"); 
            return -1; 
        } 
        //read/write loop
        status = pthread_create(&(info->untimed_thread), NULL, untimed_client, (void*)info);

    } 
    else
    {
        status = pthread_create(&(info->untimed_thread), NULL, untimed_server, (void*)info);
    }
    
    while(comm_sync==SERVER_TURN);
    return status;
}


int 
open_sock(vb_socketcall_info_t * info)
{
	struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);

    info->timed_socket->fd = syscall(__NR_socket, 
                                info->timed_socket->fd_domain,
                                info->timed_socket->fd_type,
                                info->timed_socket->fd_proto);

    clock_gettime(CLOCK_MONOTONIC, &stop);
	info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    info->call_seq[info->call_idx].syscall_num = __NR_socket;
    info->call_idx += 1;
    return info->timed_socket->fd;
}

int 
connect_sock(vb_socketcall_info_t * info)
{
    while(comm_sync!=CLIENT_TURN); //wait our turn
    int status;
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);

    status = syscall(__NR_connect,
                info->timed_socket->fd,
                info->server_addr,
                sizeof(struct sockaddr));


    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    info->call_seq[info->call_idx].syscall_num = __NR_connect;
    info->call_idx += 1;

    comm_sync=SERVER_TURN; //allow server to continue
    return status;
}
int 
accept_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);


    status = syscall(__NR_accept,
                info->timed_socket->fd,
                info->server_addr,
                sizeof(struct sockaddr));


    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    info->call_seq[info->call_idx].syscall_num = __NR_connect;
    info->call_idx += 1;
    return status;
}
int 
sendto_sock(vb_socketcall_info_t * info)
{

    while(comm_sync!=CLIENT_TURN); //wait our turn

    int status;
    char msg[1024]; 
    struct timespec start, stop;
    
    sprintf(msg, "This is a default send message #%i\n", info->call_idx-2);


    clock_gettime(CLOCK_MONOTONIC, &start);

    status = syscall(__NR_sendto, 
                info->timed_socket->fd,
                msg,1024,0, NULL, 0
                );

    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    info->call_seq[info->call_idx].syscall_num = __NR_sendto;
    info->call_idx += 1;

    comm_sync=SERVER_TURN; //allow server to continue

    return status;
    
}
int 
recvfrom_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
sendmsg_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
read_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    char buffer[1024] = {0}; 

    clock_gettime(CLOCK_MONOTONIC, &start);

    status = syscall(__NR_read, 
                info->timed_socket->fd,
                buffer, 1024
                );

    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    info->call_seq[info->call_idx].syscall_num = __NR_read;
    info->call_idx += 1;
    return status;
}
int 
shutdown_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
bind_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;

    clock_gettime(CLOCK_MONOTONIC, &start);

    status = syscall(__NR_bind,
                info->timed_socket->fd,
                info->server_addr,
                sizeof(struct sockaddr));

    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
        info->call_seq[info->call_idx].syscall_num = __NR_bind;
    info->call_idx += 1;
    return status;
}
int 
listen_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);

    status = syscall(__NR_listen,
                info->timed_socket->fd,
                info->server_backlog
                );

    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
       info->call_seq[info->call_idx].syscall_num = __NR_listen;
    info->call_idx += 1; 
    return status;
}
int 
getsockname_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
getpeername_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
socketpair_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
setsockopt_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int 
getsockopt_sock(vb_socketcall_info_t * info)
{
    int status;
    struct timespec start, stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);



    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    return status;
}
int
close_sock(vb_socketcall_info_t * info)
{
    done_sending=1;
    int status;
    struct timespec start, stop;
    enum socket_call_idx call = _close;
    clock_gettime(CLOCK_MONOTONIC, &start);

    status = syscall(__NR_close,info->timed_socket->fd);

    clock_gettime(CLOCK_MONOTONIC, &stop);
    info->call_seq[info->call_idx].time_in = TO_NSECS(start.tv_sec,start.tv_nsec);
    info->call_seq[info->call_idx].time_out = TO_NSECS(stop.tv_sec,stop.tv_nsec);
    info->call_seq[info->call_idx].syscall_num = __NR_close;
    info->call_idx += 1;
    return status;

}
