/*
 * compiler.h - compiler specific macros
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 * Calling file needs to have previously included "gpsd_config.h"
 */
#ifndef _GPSD_COMPILER_H_
#define _GPSD_COMPILER_H_

/*
 * Tell GCC that we want thread-safe behavior with _REENTRANT;
 * in particular, errno must be thread-local.
 * Tell POSIX-conforming implementations with _POSIX_THREAD_SAFE_FUNCTIONS.
 * See http://www.unix.org/whitepapers/reentrant.html
 */
#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS
#endif


/* Macro for declaring function with printf-like arguments. */
# if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
#define PRINTF_FUNC(format_index, arg_index) \
    __attribute__((__format__(__printf__, format_index, arg_index)))
# else
#define PRINTF_FUNC(format_index, arg_indx)
#endif

/* Macro for declaring function arguments unused. */
#if defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

// Macro to suppress FALLTHROUGH warnings
#if defined(__GNUC__) && __GNUC__ >= 7
    #define FALLTHROUGH __attribute__((fallthrough));
// At least one Clang 6 version advertises fallthrough and then chokes on it
#elif defined(__clang__) && __clang_major__ > 6
    #ifndef __has_attribute         // For backwards compatibility
        #define __has_attribute(x) 0
    #endif

    #if __has_attribute(fallthrough)
        #define FALLTHROUGH __attribute__((fallthrough));
    #endif
#endif

#ifndef FALLTHROUGH
    #define FALLTHROUGH
#endif

/*
 * Macro for compile-time checking if argument is an array.
 * It expands to constant expression with int value 0.
 */
#if defined(__GNUC__)
#define COMPILE_CHECK_IS_ARRAY(arr) ( \
    0 * (int) sizeof(({ \
        struct { \
            int unused_int; \
            __typeof__(arr) unused_arr; \
        } zero_init = {0}; \
        __typeof__(arr) arg_is_not_array UNUSED = { \
            zero_init.unused_arr[0], \
        }; \
        1; \
    })) \
)
#else
#define COMPILE_CHECK_IS_ARRAY(arr) 0
#endif

/* Needed because 4.x versions of GCC are really annoying */
#define ignore_return(funcall) \
    do { \
        UNUSED ssize_t locresult = (funcall); \
        assert(locresult != -23); \
    } while (0)

#ifdef __COVERITY__
    /* do nothing */
#elif defined(__cplusplus)
    /* we are C++ */
    #if __cplusplus >= 201103L
        /* C++ before C++11 can not handle stdatomic.h or atomic */
        /* atomic is just C++ for stdatomic.h */
        #include <atomic>
    #endif
#elif defined(HAVE_STDATOMIC_H)
    /* we are C and atomics are in C98 and newer */
    #include <stdatomic.h>
#elif defined(HAVE_OSATOMIC_H)
    /* do it the OS X way */
    #include <libkern/OSAtomic.h>
#endif /* HAVE_OSATOMIC_H */

static inline void memory_barrier(void)
/* prevent instruction reordering across any call to this function */
{
#ifdef __COVERITY__
    /* do nothing */
#elif defined(__cplusplus)
  /* we are C++ */
  #if __cplusplus >= 201103L
    /* C++11 and later has atomics, earlier do not */
    std::atomic_thread_fence(std::memory_order_seq_cst);
  #endif
#elif defined HAVE_STDATOMIC_H
    /* we are C and atomics are in C98 and newer */
    atomic_thread_fence(memory_order_seq_cst);
#elif defined(HAVE_OSATOMIC_H)
    /* do it the OS X way */
    OSMemoryBarrier();
#elif defined(__GNUC__)
    __asm__ __volatile__ ("" : : : "memory");
#endif
}

#endif /* _GPSD_COMPILER_H_ */
// vim: set expandtab shiftwidth=4
