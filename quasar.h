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

#define ECHUNK	100		// chunk of events epoll handles at a time
#define WEBHDR	512		// HTTP response header buffer size
#define BHSIZE	998888999	// blackhole size
#define PERIOD	1		// logically report every second

struct	qstat {
	int		efd;	// epoll file descriptor
	int		cox;	// connections closed
	uint64_t	rps;
	uint64_t	bps;
pthread_mutex_t		mx;
	}	*th;		// thread specific data
int		scf;		// server-side connection closure flag
int		sre;		// last send_req() error
int		coe;		// new connection error
int		hl, gl;		// HOST and GET sizes
char		*hp, *gp;	// HOST and GET pointers
uint64_t	vernum, vercur;	// version control of HTTP GET

void	*wkr(void*), send_req(int, struct qstat*);
