#include "quasar.h"

void *wkr(void *arg)
{
    int n, i, fd, got;
    long long k;                // content length
    char *ptr, *ptc, buf[WEBHDR];
    uint64_t cd;                // connection data
    struct qstat *tt = arg;
    struct epoll_event events[ECHUNK];
    struct iovec iov = {.iov_base = buf,.iov_len = BHSIZE };
    struct msghdr mh = {.msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = MSG_TRUNC
    };

    tt->twoh = 0;
    for (; running;) {
        if ((n = epoll_wait(tt->efd, events, ECHUNK, -1)) < 0) {
            perror("epoll_wait");
            exit(1);
        }
        for (i = 0; i < n; i++) {
            cd = events[i].data.u64;
            fd = cd & 0xFFFFFFFF;
            cd >>= 32;          // Bytes to be read
            if (cd) {
                // We blackhole incoming data:
                while (((got =
                         syscall(SYS_recvmsg, fd, &mh, MSG_TRUNC)) < 0)
                       && (errno == EINTR)) {
                    ;
                }
                // printf("data = %s\n", buf);
                if (got < 0) {
                    coe = errno;
                    while ((close(fd) < 0) && (errno == EINTR)) {
                        ;
                    }
                    pthread_mutex_lock(&tt->mx);
                    tt->cox++;
                    pthread_mutex_unlock(&tt->mx);
                    continue;
                }
                if (got == 0) {
                    while ((close(fd) < 0) && (errno == EINTR)) {
                        ;
                    }
                    scf = 1;
                    pthread_mutex_lock(&tt->mx);
                    tt->cox++;
                    pthread_mutex_unlock(&tt->mx);
                    continue;
                }
                pthread_mutex_lock(&tt->mx);
                tt->bps += got;
                // printf("%d, bps = %8d, cd = %8d, k = %8d, got = %8d\n", tt->id, tt->bps, cd, k, got);
                pthread_mutex_unlock(&tt->mx);
                if ((cd -= got) <= 0) {
                    send_req(fd, tt);
                    cd = 0;
                }
            } else {
                // we receive HTTP headers:
                while (((got = recv(fd, buf, WEBHDR - 1, 0)) < 0)
                       && (errno == EINTR)) {
                    ;
                }
                if (got < 0) {
                    coe = errno;
                    while ((close(fd) < 0) && (errno == EINTR)) {
                        ;
                    }
                    pthread_mutex_lock(&tt->mx);
                    tt->cox++;
                    pthread_mutex_unlock(&tt->mx);
                    continue;
                }
                if (got == 0) {
                    while ((close(fd) < 0) && (errno == EINTR)) {
                        ;
                    }
                    scf = 1;
                    pthread_mutex_lock(&tt->mx);
                    tt->cox++;
                    pthread_mutex_unlock(&tt->mx);
                    continue;
                }
                pthread_mutex_lock(&tt->mx);
                tt->bps += got;
                pthread_mutex_unlock(&tt->mx);
                buf[got] = '\0';        // Make C string
                if (buf[9] == '2') {
                    tt->twoh++;
                }
                if ((ptr = strstr(buf, "\r\n\r\n")) == NULL) {
                    fputs("Bad response header:\n", stderr);
                    fputs(buf, stderr);
                    exit(1);
                }
                *ptr = '\0';
                if ((ptc = strstr(buf, "Content-Length")) != NULL) {
                    if (sscanf(ptc, "%*s %llu", &k) <= 0) {
                        perror("sscanf");
                        fputs(strcat(ptc, "\n"), stderr);
                        exit(1);
                    }
                    cd = k - (&buf[got] - ptr - 4);     // data bytes to receive
                    // printf("%d, bps = %8d, cd = %8d, k = %8d, got = %8d\n", tt->id, tt->bps, cd, k, got);
                    if (cd >> 32) {
                        fputs("HTTP object must not exceed 4.2GB\n",
                              stderr);
                        exit(1);
                    }
                } else {
                    if (buf[9] == '2') {        // with HTTP code 2xx
                        fputs("Content-Length header must be specified\n",
                              stderr);
                        exit(1);
                    }
                    // not 2xx and no Content-Length header => ok, no more data to come
                    cd = 0;
                }
                if (cd <= 0) {
                    send_req(fd, tt);
                    cd = 0;
                }
            }
            // We don't get 0 on read() and closure triggers RST:
            /*if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
               while (close(fd) < 0 && errno == EINTR);
               scf = 1;
               pthread_mutex_lock(&tt->mx);
               tt->cox++;
               pthread_mutex_unlock(&tt->mx);
               continue;
               } */
            events[i].data.u64 = (cd << 32) | fd;
            events[i].events = EPOLLIN;
            /* No error check in case fd is reset by send_req(): */
            epoll_ctl(tt->efd, EPOLL_CTL_MOD, fd, &events[i]);
        }
    }
    pthread_exit(NULL);
}
