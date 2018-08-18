#include "quasar.h"
#include <signal.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

void sig_handler(int signo)
{
    running = 0;
}

/*
 * HTTP server load testing tool
 * server must provide Content-Length header
 * URL: [http://]<host|ip>[:<port>]/object[?|&|_]
 * may be appended with ?, &, or _<version>
 * nofile: /etc/security/limits.conf
 * tee: stdbuf -o L ./quasar 2 20 0 "http://host/qq?" |tee qt
 */
int main(int argc, char **argv)
{

    int i, j, tnum, allcox, connum, runtime, k, cfd, ramp = 0;
    int updfreq = PERIOD, mode = MODE_READ;
    uint64_t allrps = 0, allbps = 0, alltwoh = 0;
    float alllat = 0;
    char *p, hbuf[256], prt[8], erbuf[256];
    pthread_t tid;
    struct timespec tick, tock, start;
    struct addrinfo hints, *res, *r;
    struct epoll_event ev = {.events = EPOLLIN };

    signal(SIGINT, sig_handler);

    if (argc < 4) {
      meh:fputs
            ("Usage: ./quasar <#threads> <#runtime> <URL> <fill | read> <#reportfreq>\n",
             stderr);
        exit(1);
    }
    if ((sscanf(argv[1], "%d", &tnum) < 1)
        || (sscanf(argv[2], "%d", &runtime) < 1))
        goto meh;
    if (tnum < 1) {
        fputs("At least 1 thread, please!\n", stderr);
        exit(1);
    }
    if ((p = strstr(argv[3], "://")) != NULL) {
        hp = p + 3;
    } else {
        hp = argv[3];
    }
    if ((gp = strstr(hp, "/")) == NULL) {
        fputs("URL needs closing slash!\n", stderr);
        exit(1);
    }
    gl = strlen(gp);
    if (((p = strstr(hp, ":")) != NULL) && (p < gp)) {
        i = gp - p - 1;         // length of the port string
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

    if (argc > 4) {
        if (!strncasecmp(argv[4], "fill", 4))
            mode = MODE_FILL;
        else if (!strncasecmp(argv[4], "read", 4))
            mode = MODE_READ;
        else
            goto meh;
    }

    if (argc > 5) {
        if (sscanf(argv[5], "%d", &updfreq) < 1) {
            goto meh;
        }
    }

    /* Start running */
    running = 1;
    ramp = tnum;

    /* Parsing completed, setting up data structures: */
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((i = getaddrinfo(hp, prt, &hints, &res)) != 0) {
        fputs("getaddrinfo: ", stderr);
        fputs(gai_strerror(i), stderr);
        freeaddrinfo(res);
        exit(1);
    }
    /* th[tnum] is a structure for the main() thread: */
    if ((th = calloc(tnum + 1, sizeof(struct qstat))) == NULL) {
        fputs("No memory!\n", stderr);
        exit(1);
    }
    if (mode == MODE_FILL) {
        for (i = 0; i < tnum; ++i) {
            th[i].curver = 0;
            for (j = 0; j < MAXVER; ++j) {
                th[i].ver[j] = j;
            }
        }
    } else {                    // mode == MODE_READ
        /* Allocate MAXVER random numbers per thread */
        for (i = 0; i < tnum; i++) {
            srand(i);
            th[i].curver = 0;
            for (j = 0; j < MAXVER; ++j) {
                int rnd = rand() % (MAXVER + 1);
                th[i].ver[j] = (uint64_t) rnd;
            }
        }
    }

    for (i = 0; i < tnum; i++) {
        if ((th[i].efd = epoll_create(ECHUNK)) < 0) {
            perror("epoll_create");
            exit(1);
        }
        pthread_mutex_init(&th[i].mx, NULL);
        if ((errno = pthread_create(&tid, NULL, wkr, &th[i])) != 0) {
            perror("pthread_create");
            exit(1);
        }
        th[i].id = i;
        th[i].tid = tid;
    }
    pthread_mutex_init(&th[tnum].mx, NULL);     // dummy mutex for this thread
    /* Ramping up: */
    fprintf(stdout, "Number of TCP connections: %d\n", tnum);
    fprintf(stdout, "Update frequency: %ds\n", updfreq);
    fprintf(stdout, "mode = %s\n", (mode == MODE_FILL) ? "fill" : "read");
    fprintf(stdout,
            "------------------------------------------------------------\n");
    fprintf(stdout,
            " Elapsed (s)       RPS      200OKs       kbps      Lat (ms)\n");
    clock_gettime(CLOCK_MONOTONIC, &tick);
    start = tick;
    tick.tv_sec -= 1;
    connum = k = 0;             // k is round-robin thread index
    for (; running;) {
        allcox = allrps = allbps = alllat = alltwoh = 0;
        if (clock_gettime(CLOCK_MONOTONIC, &tock) < 0) {
            perror("clock_gettime");
            exit(1);
        }
        for (i = 0; i <= tnum; i++) {
            pthread_mutex_lock(&th[i].mx);
            allcox += th[i].cox;
            allrps += th[i].rps;
            allbps += th[i].bps;
            alltwoh += th[i].twoh;
            th[i].cox = 0;
            th[i].rps = 0;
            th[i].bps = 0;
            th[i].twoh = 0;
            pthread_mutex_unlock(&th[i].mx);
        }
        connum -= allcox;       // If some connections die
        /* tick =  tock - tick: */
        tick.tv_sec = tock.tv_sec - tick.tv_sec;
        if ((tick.tv_nsec = tock.tv_nsec - tick.tv_nsec) < 0) {
            tick.tv_sec--;
            tick.tv_nsec += 1000000000;
        }
        tick.tv_sec = tick.tv_nsec / 1000000 + tick.tv_sec * 1000;      //milliseconds
        allrps = allrps * 1000 / tick.tv_sec;   // rps
        allbps = allbps * 8 / tick.tv_sec;      // kbps
        alllat = (allrps) ?
            (float) tick.tv_sec / (float) alltwoh / (float) updfreq : 0;
        /* Presentation block (connum rps bps scf sre coe): */
        for (i = 0; tick.tv_sec > (i * 1000 + 500) * updfreq; i++) {
            printf(" %*ld  %*d  %*d  %*d %*f",
                   11, (tock.tv_sec - start.tv_sec),
                   8, (int) allrps,
                   10, (int) alltwoh, 9, (int) allbps, 13, (float) alllat);
            if (scf) {
                printf(" *SRVCLOSE*");
            }
            if (sre) {
                strerror_r(sre, erbuf, sizeof(erbuf));
                printf(" SND: %s", erbuf);
            }
            if (coe) {
                strerror_r(coe, erbuf, sizeof(erbuf));
                printf(" CON: %s", erbuf);
            }
            printf("\n");
        }
        fflush(stdout);
        coe = sre = scf = 0;    // We handle flags off mutex
        /* Connection block: */
        for (i = 0; ((argc < 6) && (i < ramp)) || ((argc == 6)
                                                   && (connum < ramp));
             i++) {
            r = res;
            do {
                cfd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
                if (cfd < 0) {
                    continue;
                }
                if (connect(cfd, r->ai_addr, r->ai_addrlen) == 0) {
                    break;
                }
                while ((close(cfd) < 0) && (errno == EINTR)) {
                    ;
                }
            } while ((r = r->ai_next) != NULL);
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
            if (++k >= tnum) {
                k = 0;
            }
            send_req(cfd, &th[tnum]);   // Here the dummy mutex fires
        }
        memcpy(&tick, &tock, sizeof(tick));
        tock.tv_sec += updfreq;
        while ((clock_nanosleep
                (CLOCK_MONOTONIC, TIMER_ABSTIME, &tock, NULL) < 0)
               && (errno == EINTR)) {
            ;
        }
        if ((tock.tv_sec - start.tv_sec) > runtime) {
            running = 0;
            break;
        }
    }
    for (i = 0; i < tnum; i++) {
        pthread_join(th[i].tid, NULL);
    }
    free(th);
    return 0;
}
