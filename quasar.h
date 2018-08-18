#include <sys/epoll.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <strings.h>
#include <sys/syscall.h>

#define ECHUNK	100             // chunk of events epoll handles at a time
#define WEBHDR	512             // HTTP response header buffer size
#define BHSIZE	998888999       // blackhole size
#define PERIOD	1               // logically report every second
#define MAXVER	1000

/* modes */
#define MODE_FILL	0
#define MODE_READ	1

struct qstat {
    int id;                     // thread id
    pthread_t tid;              // pthread instance
    int efd;                    // epoll file descriptor
    int cox;                    // connections closed
    uint64_t twoh;              // 200 OKs;
    uint64_t rps;
    uint64_t bps;
    pthread_mutex_t mx;
    uint64_t curver;            // last count
    uint64_t ver[MAXVER];
} *th;                          // thread specific data
int scf;                        // server-side connection closure flag
int sre;                        // last send_req() error
int coe;                        // new connection error
int hl, gl;                     // HOST and GET sizes
char *hp, *gp;                  // HOST and GET pointers
int running;                    // global running flag

void *wkr(void *), send_req(int, struct qstat *);


