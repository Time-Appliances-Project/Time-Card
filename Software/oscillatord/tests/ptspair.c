/**
 * @file ptspair.c
 * @brief Each read on a pts, writes on the other's ring buffer.
 *
 * @date 5 mai 2015
 * @author carrier.nicolas0@gmail.com
 * @copyright MIT license, please refer to COPYING
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif /* _GNU_SOURCE */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/epoll.h>
#include <termios.h>

#include "ptspair.h"

static struct termios cooked_tios = {
    .c_iflag = TTYDEF_IFLAG,
    .c_oflag = TTYDEF_OFLAG,
    .c_lflag = TTYDEF_LFLAG,
    .c_cflag = TTYDEF_CFLAG,
    .c_cc =
        {
               [VINTR]    = CINTR,
               [VQUIT]    = CQUIT,
               [VERASE]   = CERASE,
               [VKILL]    = CKILL,
               [VEOF]     = CEOF,
               [VTIME]    = CTIME,
               [VMIN]     = CMIN,
               [VSWTC]    = _POSIX_VDISABLE,
               [VSTART]   = CSTART,
               [VSTOP]    = CSTOP,
               [VSUSP]    = CSUSP,
               [VEOL]     = CEOL,
               [VREPRINT] = CREPRINT,
               [VDISCARD] = CDISCARD,
               [VWERASE]  = VWERASE,
               [VLNEXT]   = CLNEXT,
               [VEOL2]    = CEOL,
               },
};
static struct termios                    raw_tios;

__attribute__((constructor)) static void init_ptspair(void) {
    cfsetspeed(&cooked_tios, B38400);
    cfmakeraw(&raw_tios);
}

static void clean_pts(struct pts* pts) {
    if (pts == NULL)
        return;

    if (pts->writer > 0) {
        close(pts->writer);
        pts->writer = -1;
    }
    if (pts->master > 0) {
        close(pts->master);
        pts->master = -1;
    }
    memset(pts, 0, sizeof(*pts));
}

static int configure_pts(struct pts* pts, const struct termios* tios) {
    int ret;

    ret = tcsetattr(pts->writer, TCSANOW, tios);
    if (ret < 0)
        return -errno;

    return 0;
}

static int init_pts(struct pts* pts) {
    int ret;

    memset(pts, 0, sizeof(*pts));
    pts->writer = -1;
    pts->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pts->master < 0) {
        ret = -errno;
        goto err;
    }
    ret = grantpt(pts->master);
    if (ret < 0) {
        ret = -errno;
        goto err;
    }
    ret = unlockpt(pts->master);
    if (ret < 0) {
        ret = -errno;
        goto err;
    }
    ret = ptsname_r(pts->master, pts->slave_path, PTSPAIR_PATH_MAX);
    /* a buffer which is too short sets errno to ERANGE */
    if (ret < 0) {
        ret = -errno;
        goto err;
    }
    pts->writer = open(pts->slave_path, O_WRONLY | O_CLOEXEC);
    if (pts->writer == -1) {
        ret = -errno;
        goto err;
    }

    return configure_pts(pts, &cooked_tios);
err:
    clean_pts(pts);

    return ret;
}

static int pts_epoll_ctl(const struct ptspair* ptspair, struct pts* pts, int op, int evts) {
    struct epoll_event event = {
        .events = evts,
        .data =
            {
                   .ptr = pts,
                   },
    };

    return epoll_ctl(ptspair->epollfd, op, pts->master, &event);
}

static int register_pts_read(const struct ptspair* ptspair, struct pts* pts) {
    return pts_epoll_ctl(ptspair, pts, EPOLL_CTL_ADD, EPOLLIN);
}

static int register_pts_write(const struct ptspair* ptspair, struct pts* pts) {
    return pts_epoll_ctl(ptspair, pts, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);
}

static int unregister_pts_write(const struct ptspair* ptspair, struct pts* pts) {
    return pts_epoll_ctl(ptspair, pts, EPOLL_CTL_MOD, EPOLLIN);
}

static char* write_start(struct buffer* buf) {
    return buf->buf + buf->end;
}

static int write_length(const struct buffer* buf) {
    if (buf->end < buf->start)
        return buf->start - buf->end;

    return PTSPAIR_BUFFER_SIZE - buf->end;
}

static void written_update(struct buffer* buf, int added) {
    buf->end += added;
    buf->end %= PTSPAIR_BUFFER_SIZE;
    if (buf->end == buf->start)
        buf->full = true;
}

static char* read_start(struct buffer* buf) {
    return buf->buf + buf->start;
}

static int read_length(struct buffer* buf) {
    if (buf->end < buf->start)
        return PTSPAIR_BUFFER_SIZE - buf->start;
    if (buf->end == buf->start)
        return buf->full ? PTSPAIR_BUFFER_SIZE - buf->start : 0;

    return buf->end - buf->start;
}

static void read_update(struct buffer* buf, int consumed) {
    if (buf->end == buf->start)
        buf->full = false;
    buf->start += consumed;
    buf->start %= PTSPAIR_BUFFER_SIZE;
}

static struct pts* get_other_pts(struct ptspair* ptspair, const struct pts* pts) {
    if (pts->master == ptspair->pts[PTSPAIR_FOO].master)
        return ptspair->pts + PTSPAIR_BAR;
    else
        return ptspair->pts + PTSPAIR_FOO;
}

static int process_in_event(struct ptspair* ptspair, const struct pts* pts) {
    ssize_t        sret;
    char*          start;
    int            len;
    bool           is_registered;
    struct pts*    other_pts;
    struct buffer* buf;

    other_pts = get_other_pts(ptspair, pts);
    buf       = &other_pts->buf;
    start     = write_start(buf);
    len       = write_length(buf);
    if (len == 0)
        return -ENOBUFS;
    is_registered = read_length(buf) != 0;
    sret          = read(pts->master, start, len);
    if (sret < 0)
        return -errno;
    written_update(buf, sret);
    if (is_registered)
        return 0;

    return register_pts_write(ptspair, other_pts);
}

static int process_out_event(struct ptspair* ptspair, struct pts* pts) {
    ssize_t        sret;
    char*          start;
    int            len;
    struct buffer* buf = &pts->buf;

    start              = read_start(buf);
    len                = read_length(buf);
    sret               = write(pts->master, start, len);
    if (sret < 0)
        return -errno;
    read_update(buf, sret);
    if (read_length(buf) == 0)
        return unregister_pts_write(ptspair, pts);

    return 0;
}

/* returns the error which occurred last */
static int process_events(struct ptspair* ptspair, struct epoll_event* events, int events_nb) {
    int                 ret;
    struct epoll_event* e;
    struct pts*         evt_src_pts;
    int                 error = 0;

    while (events_nb--) {
        e           = events + events_nb;
        evt_src_pts = e->data.ptr;
        if (e->events & EPOLLIN) {
            ret = process_in_event(ptspair, evt_src_pts);
            if (ret < 0)
                error = ret;
        }
        if (e->events & EPOLLOUT) {
            ret = process_out_event(ptspair, evt_src_pts);
            if (ret < 0)
                error = ret;
        }
        if (e->events & EPOLLERR)
            /*
             * I couldn't find clues concerning the semantics of the
             * EPOLLERR event for a pts. I even tried grepping the
             * kernel, but with no luck, I guess there is nothing
             * left to do but cleanup
             */
            return -EIO;
        if (e->events & EPOLLHUP)
            /*
             * this should normally not happen, since it means that
             * no opened file descriptor remain open on the slave
             * and we keep one. But in case someone accidentally
             * closed it...
             */
            return -EIO;
    }

    return error;
}

int ptspair_init(struct ptspair* ptspair) {
    int ret;

    if (ptspair == NULL)
        return -EINVAL;

    ptspair->epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (ptspair->epollfd == -1)
        return -errno;

    ret = init_pts(ptspair->pts + PTSPAIR_FOO);
    if (ret < 0)
        goto err;
    ret = init_pts(ptspair->pts + PTSPAIR_BAR);
    if (ret < 0)
        goto err;

    ret = register_pts_read(ptspair, ptspair->pts + PTSPAIR_FOO);
    if (ret < 0)
        goto err;
    ret = register_pts_read(ptspair, ptspair->pts + PTSPAIR_BAR);
    if (ret < 0)
        goto err;

    return 0;
err:
    ptspair_clean(ptspair);

    return ret;
}

const char* ptspair_get_path(const struct ptspair* ptspair, enum pts_index index) {
    errno = EINVAL;

    if (ptspair == NULL)
        return NULL;

    switch (index) {
    case PTSPAIR_FOO:
    case PTSPAIR_BAR:
        return ptspair->pts[index].slave_path;
    default:
        return NULL;
    }
}

int ptspair_get_writer_fd(const struct ptspair* ptspair, enum pts_index index) {
    errno = EINVAL;

    if (ptspair == NULL)
        return -1;

    switch (index) {
    case PTSPAIR_FOO:
    case PTSPAIR_BAR:
        return ptspair->pts[index].writer;
    default:
        return -1;
    }
}

int ptspair_raw(struct ptspair* ptspair, enum pts_index index) {
    if (ptspair == NULL)
        return -EINVAL;

    switch (index) {
    case PTSPAIR_FOO:
    case PTSPAIR_BAR:
        return configure_pts(ptspair->pts + index, &raw_tios);
    default:
        return -EINVAL;
    }
}

int ptspair_cooked(struct ptspair* ptspair, enum pts_index index) {
    if (ptspair == NULL)
        return -EINVAL;

    switch (index) {
    case PTSPAIR_FOO:
    case PTSPAIR_BAR:
        return configure_pts(ptspair->pts + index, &cooked_tios);
    default:
        return -EINVAL;
    }
}

int ptspair_get_fd(const struct ptspair* ptspair) {
    if (ptspair == NULL)
        return -EINVAL;

    return ptspair->epollfd;
}

int ptspair_process_events(struct ptspair* ptspair) {
#define PTSPAIR_EVENTS_NB 4
    int                ret;
    struct epoll_event events[PTSPAIR_EVENTS_NB];

    if (ptspair == NULL)
        return -EINVAL;

    memset(events, 0, PTSPAIR_EVENTS_NB * sizeof(*events));
    ret = epoll_wait(ptspair->epollfd, events, PTSPAIR_EVENTS_NB, 0);
    if (ret < 0)
        return -errno;

    return process_events(ptspair, events, ret);
#undef PTSPAIR_EVENTS_NB
}

void ptspair_clean(struct ptspair* ptspair) {
    if (ptspair == NULL)
        return;

    if (ptspair->epollfd != 0)
        close(ptspair->epollfd);
    clean_pts(ptspair->pts + PTSPAIR_BAR);
    clean_pts(ptspair->pts + PTSPAIR_FOO);
    memset(ptspair, 0, sizeof(*ptspair));
}
