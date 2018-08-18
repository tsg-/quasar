#include "quasar.h"

void send_req(int fd, struct qstat *tt)
{
    char ver[64];
    struct iovec iov[6] = {
        {.iov_base = "GET ",.iov_len = 4},
        {.iov_base = gp,.iov_len = gl},
        {.iov_base = ver,.iov_len = 0},
        {.iov_base = " HTTP/1.1\r\nHost: ",.iov_len = 17},
        {.iov_base = hp,.iov_len = hl},
        {.iov_base =
         "\r\nUser-Agent: Quasar\r\nConnection: keep-alive\r\n\r\n",
         .iov_len = 48}
    };
    struct msghdr mh = {.msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = iov,
        .msg_iovlen = 6,        //?sizeof(iov),
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };
    struct linger xx = {.l_onoff = 1,.l_linger = 0 };
    int r = 0, x = 0;

    // int a = (int) ++tt->curver % MAXVER;
    x = tt->ver[(tt->curver % (MAXVER - 1))];
    ++tt->curver;

    // format /test/t0009_00522.html
    snprintf(ver, sizeof(ver), "%04llu_%05llu.html",
             (unsigned long long int) tt->id,
             (unsigned long long int) x);
    if ((gp[gl - 1] == '?') || (gp[gl - 1] == '&') || (gp[gl - 1] == '_')) {
        iov[2].iov_len = strlen(ver);
    } else {
        iov[2].iov_len = 0;
    }
    while (((r = sendmsg(fd, &mh, MSG_DONTWAIT | MSG_NOSIGNAL)) < 0)
           && (errno == EINTR)) {
        ;
    }
    if (r < 0) {
        sre = errno;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &xx, sizeof(xx));
        while ((close(fd) < 0) && (errno == EINTR)) {
            ;
        }
        pthread_mutex_lock(&tt->mx);
        tt->cox++;
        pthread_mutex_unlock(&tt->mx);
    } else {
        pthread_mutex_lock(&tt->mx);
        tt->rps++;
        pthread_mutex_unlock(&tt->mx);
    }
}
