#include "quasar.h"
/*
 * HTTP server load testing tool
 * server must provide Content-Length header
 * URL: [http://]<host|ip>[:<port>]/object[?|&]
 * may be appended with ?v or &v = <version>
 * nofile: /etc/security/limits.conf
 * tee: stdbuf -o L ./quasar 2 20 0 "http://host/qq?" |tee qt
 */
int
main(int argc, char **argv)
{
	int	i, tnum, ramp, allcox, connum, k, cfd;
	uint64_t	allrps, allbps;
	char	*p, hbuf[256], prt[8], erbuf[256];
	pthread_t	tid;
struct	timespec	tick, tock;
struct	addrinfo	hints, *res, *r;
struct	epoll_event	ev = {.events = EPOLLIN};

	if (argc < 5) {
meh:		fputs("Usage: ./quasar <#threads> <#connAsecond> <#versions> <URL>\n", stderr);
		exit(1);
	}
	if (	sscanf(argv[1], "%d", &tnum) < 1 ||\
		sscanf(argv[2], "%d", &ramp) < 1 ||\
		sscanf(argv[3], "%lu", &vernum) < 1) goto meh;
	if (tnum < 1) {
		fputs("At least 1 thread, please!\n", stderr);
		exit(1);
	}
	if ( (p = strstr(argv[4], "://")) != NULL) hp = p + 3;
	else hp = argv[4];
	if ( (gp = strstr(hp, "/")) == NULL) {
		fputs("URL needs closing slash!\n", stderr);
		exit(1);
	}
	gl = strlen(gp);
	if ( (p = strstr(hp, ":")) != NULL && p < gp) {
		i = gp - p - 1;	// length of the port string
		memcpy(prt, p + 1, i);
		prt[i] = '\0';
		*p = '\0';
		hl = p - hp;
	} else {
		memcpy(prt, "80\0", 3);
		hl = gp - hp;
		if (hl > sizeof(hbuf) - 1) {
			fputs("Hostname is too long!\n", stderr);
			exit(1);
		}
		memcpy(hbuf, hp, hl);
		hbuf[hl] = '\0';
		hp = hbuf;
	}
	/* parsing completed, setting up data structures: */
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if ( (i = getaddrinfo(hp, prt, &hints, &res)) != 0) {
		fputs("getaddrinfo: ", stderr);
		fputs(gai_strerror(i), stderr);
		exit(1);
	}
	// th[tnum] is a structure for the main() thread:
	if ( (th = calloc(tnum + 1, sizeof(struct qstat))) == NULL) {
		fputs("No memory!\n", stderr);
		exit(1);
	}
	for (i = 0; i < tnum; i++) {
		if ( (th[i].efd = epoll_create(ECHUNK)) < 0) {
			perror("epoll_create");
			exit(1);
		}
		pthread_mutex_init(&th[i].mx, NULL);
		if ( (errno = pthread_create(&tid, NULL, wkr, &th[i])) != 0) {
			perror("pthread_create");
			exit(1);
		}
	}
	pthread_mutex_init(&th[tnum].mx, NULL);	// dummy mutex for this thread
	/* ramping up: */
	fputs("    #TCP       RPS      kbps  [SRVCLOSE|SNDERROR|CONERROR]\n", stdout);
	bzero(&tick, sizeof(tick));
	connum = k = 0;	// k is round-robin thread index
	for (; ;) {
		allcox = allrps = allbps = 0;
		if (clock_gettime(CLOCK_MONOTONIC, &tock) < 0) {
			perror("clock_gettime");
			exit(1);
		}
		for (i = 0; i <= tnum; i++) {
			pthread_mutex_lock(&th[i].mx);
			allcox += th[i].cox;
			allrps += th[i].rps;
			allbps += th[i].bps;
			th[i].cox = 0;
			th[i].rps = 0;
			th[i].bps = 0;
			pthread_mutex_unlock(&th[i].mx);
		}
		connum -= allcox;	// if some connections die
		/* tick =  tock - tick: */
		tick.tv_sec = tock.tv_sec - tick.tv_sec;
		if ( (tick.tv_nsec = tock.tv_nsec - tick.tv_nsec) < 0) {
			tick.tv_sec--;
			tick.tv_nsec += 1000000000;
		}
		tick.tv_sec = tick.tv_nsec / 1000000 + tick.tv_sec * 1000;	//milliseconds
		allrps = allrps * 1000 / tick.tv_sec;	// rps
		allbps = allbps * 8 / tick.tv_sec;	// kbps
		/* presentation block (connum rps bps scf sre coe): */
		printf("%*d  %*d  %*d ", 8, connum, 8, (int) allrps, 8, (int) allbps);
		if (scf) printf(" *SRVCLOSE*");
		if (sre) {
			strerror_r(sre, erbuf, sizeof(erbuf));
			printf(" SND: %s", erbuf);
		}
		if (coe) {
			strerror_r(coe, erbuf, sizeof(erbuf));
			printf(" CON: %s", erbuf);
		}
		printf("\n");
		fflush(stdout);
		coe = sre = scf = 0;	// we handle flags off mutex
		/* connection block: */
		for (i = 0; i < ramp; i++) {
			r = res;
			do {
				cfd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
				if (cfd < 0) continue;
				if (connect(cfd, r->ai_addr, r->ai_addrlen) == 0) break;
				while (close(cfd) < 0 && errno == EINTR);
			} while ( (r = r->ai_next) != NULL);
			if (r == NULL) {
				// connection can't be established
				coe = errno;
				break;
			}
			connum++;
			ev.data.u64 = (uint64_t) cfd;
			if (epoll_ctl(th[k].efd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
				perror("epoll_ctl_add");
				exit(1);
			}
			if (++k >= tnum) k = 0;
			send_req(cfd, &th[tnum]);	// here the dummy mutex fires
		}
		memcpy(&tick, &tock, sizeof(tick));
		tock.tv_sec += PERIOD;
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tock, NULL) < 0 && errno == EINTR);
	}
	return 0;
}
