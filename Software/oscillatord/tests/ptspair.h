/**
 * @file ptspair.h
 * @brief creates a pair of connected pts.
 *
 * Initialize a context with ptspair_init(), then register it's fd in your event
 * monitoring loop (poll, select, epoll...) for IN events. Call
 * ptspair_process_events() each time the fd fires an event.<br />
 *
 * The writer file descriptor returned by ptspair_get_writer_fd() can be used to
 * configure the corresponding terminal with tcsetattr(). But it _must not_ be
 * closed, as keeping one writer opened on a pts prevents EPOLLHUP events storm.
 * Initially, the pts are put in a cooked mode suitable to use with e.g. a
 * shell, you can use ptspair_raw() to setup one or both pts as raw pts.<br />
 * Structures must be considered as opaque and must be manipulated through API
 * functions only.<br />
 *
 * Once you're done with the context, call ptspair_clean().<br />
 *
 * Negative values from functions returning an int indicate an error and are the
 * opposite of an errno value. Functions returning a pointer indicate an error
 * by returning NULL an setting errno.
 *
 * @date 5 mai 2015
 * @author carrier.nicolas0@gmail.com
 * @copyright MIT license, please refer to COPYING
 */
#ifndef PTSPAIR_H_
#define PTSPAIR_H_
#include <limits.h>
#include <stdbool.h>

#ifndef PTSPAIR_BUFFER_SIZE
#    define PTSPAIR_BUFFER_SIZE 0x200
#endif /* PTSPAIR_BUFFER_SIZE */

#ifndef PTSPAIR_PATH_MAX
#    define PTSPAIR_PATH_MAX 0x1000
#endif /* PTSPAIR_PATH_MAX */

#define PTSPAIR_API __attribute__((visibility("default")))

enum pts_index {
    PTSPAIR_FOO,
    PTSPAIR_BAR,

    PTSPAIR_NB,
};

/* circular buffer */
struct buffer {
    char buf[PTSPAIR_BUFFER_SIZE];
    int  start;
    int  end;
    /* used to distinguish full / empty when start == end */
    bool full;
};

struct pts {
    char          slave_path[PTSPAIR_PATH_MAX];
    /*
     * stores the data read from the other pts, ready to be written to this
     * pts
     */
    struct buffer buf;
    int           master;
    /*
     * if one of the pts is closed, it's master fd will keep triggering
     * EPOLLHUP events, having a fd opened WRONLY on the slave's end
     * prevent this
     */
    int           writer;
};

struct ptspair {
    struct pts pts[PTSPAIR_NB];
    int        epollfd;
};

PTSPAIR_API int         ptspair_init(struct ptspair* ptspair);
PTSPAIR_API const char* ptspair_get_path(const struct ptspair* ptspair, enum pts_index pts_index);
/* returns the writer fd on the given pts, must NOT be closed */
PTSPAIR_API int ptspair_get_writer_fd(const struct ptspair* ptspair, enum pts_index pts_index);
PTSPAIR_API int  ptspair_raw(struct ptspair* ptspair, enum pts_index pts_index);
PTSPAIR_API int  ptspair_cooked(struct ptspair* ptspair, enum pts_index pts_index);
PTSPAIR_API int  ptspair_get_fd(const struct ptspair* ptspair);
PTSPAIR_API int  ptspair_process_events(struct ptspair* ptspair);
PTSPAIR_API void ptspair_clean(struct ptspair* ptspair);

#endif /* PTSPAIR_H_ */
