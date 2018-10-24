#ifndef __SOCKETCORPUS_H__
#define __SOCKETCORPUS_H__



#define TO_NSECS(sec,nsec)\
		((sec) * 1000000000 + (nsec))

#define ENUM_TO_NR(s)\
		__NR##s

#include <sys/types.h>
#include <time.h> 
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>


#define TOTAL_SYS_CALLS 17

#define TIME_SERVER 1
#define TIME_CLIENT 0
#define MAXNAMESIZE 15
#define SERVER_TURN 1
#define CLIENT_TURN 0

        
enum socket_call_idx
{
     _socket,
     _connect, 
     _accept,
     _sendto,
     _read,
     _recvfrom,
     _sendmsg,
     _recvmsg,
     _shutdown,
     _bind,
     _listen,
     _getsockname,
     _getpeername,
     _socketpair,
     _setsockopt,
     _getsockopt,
     _close
};

typedef struct{
    int connection_domain;
    int connection_type;
    int connection_proto;
    int connection_port;
    char** program_list;
    int list_length;
}socket_program_t;


typedef struct{
	int fd;
	int fd_domain;
    int fd_type;
    int fd_proto;

}vb_socket_t;


typedef struct {
	int syscall_num;
	unsigned long long time_in;
    unsigned long long time_out;

}time_data_t;

typedef struct {
    vb_socket_t* untimed_socket;
    vb_socket_t* timed_socket;
    //untimed opposite data
    int timer_choice;
    pthread_t untimed_thread;
    // data stroage
    int call_idx;
    time_data_t * call_seq;
    int call_seq_len;
    //connection info
    struct sockaddr_in* server_addr;
    int addrlen;
    int server_backlog;
    socket_program_t* program_info;

    int connection_failed;

} vb_socketcall_info_t;



int init_vb_socketcall_info_t(vb_socketcall_info_t * info);
int deinit_vb_socketcall_info_t(vb_socketcall_info_t * info);

int begin_untimed_program(vb_socketcall_info_t * info);

int open_sock(vb_socketcall_info_t * info);
int connect_sock(vb_socketcall_info_t * info);
int accept_sock(vb_socketcall_info_t * info);
int sendto_sock(vb_socketcall_info_t * info);
int recvfrom_sock(vb_socketcall_info_t * info);
int sendmsg_sock(vb_socketcall_info_t * info);
int recvmsg_sock(vb_socketcall_info_t * info);
int shutdown_sock(vb_socketcall_info_t * info);
int bind_sock(vb_socketcall_info_t * info);
int listen_sock(vb_socketcall_info_t * info);
int getsockname_sock(vb_socketcall_info_t * info);
int getpeername_sock(vb_socketcall_info_t * info);
int socketpair_sock(vb_socketcall_info_t * info);
int setsockopt_sock(vb_socketcall_info_t * info);
int getsockopt_sock(vb_socketcall_info_t * info);
int close_sock(vb_socketcall_info_t * info);

#endif
