#ifndef CONFIG_H_SEEN
#define CONFIG_H_SEEN

#define __STDC_FORMAT_MACROS

/* Define to 1 if you have the `event' library. */
#define HAVE_LIBEVENT 1

/* Set to 1 if EVENT_BASE_FLAG_PRECISE_TIMER is defined. */
#define HAVE_DECL_EVENT_BASE_FLAG_PRECISE_TIMER 1

/* Define to 1 if you have the `pthread' library. */
#define HAVE_LIBPTHREAD 1

/* Define to 1 if you have the `rt' library. */
#define HAVE_LIBRT 1

/* Define to 1 if you have the `zmq' library. */
/* #undef HAVE_LIBZMQ */

/* Define to 1 if the system has the function `pthread_barrier_init'. */
#define HAVE_PTHREAD_BARRIER_INIT 1

#endif /* CONFIG_H_SEEN */
