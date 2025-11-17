#ifndef __monloop_h__
#define __monloop_h__
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/timerfd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONLOOP_LINELEN 4096

struct monloop_timer;

typedef struct monloop {
  char               line[MONLOOP_LINELEN]; // line buffer
  void              *data;                  // user data pointer
  FILE              *fp;                    // filepointer for fprintf calls
  struct monloop_timer *timers;             // linked list of timers
  pthread_t          tid;                   // monitor thread
  pthread_barrier_t  barrier;	            // barrier for thread start
  int     	     epollfd;               // epoll file descriptor
  int                fd;                    // file descriptor for monitor i/o 
  int                n;                     // number of bytes buffered
  bool               silent;                // silent do produce output
  bool               running;               // true after mon thread started         
} monloop_t;

typedef long (*monloop_cmd_t)(monloop_t *, int);
typedef long (*monloop_timer_func_t)(struct monloop_timer *, int);

typedef struct monloop_timer {
  int                   fd;
  monloop_timer_func_t  func;
  monloop_t            *monloop;
  struct monloop_timer *next;
} monloop_timer_t;

#define ML_PRINT(this, fmt, ...) {				\
  if (!this->silent) fprintf(this->fp, fmt, __VA_ARGS__);	\
  }

int monloop_start(monloop_t *, void *data, int fd, FILE *fp, bool silent);
int monloop_join(monloop_t *, void **exit_status);
monloop_timer_t * monloop_addtimer(monloop_t *, int clockid,
				   monloop_timer_func_t tfunc); 
int monloop_removetimer(monloop_t *, monloop_timer_t *timer);
// convience functions for writting commands that work with data in files
int monloop_mapfile(const char *filename, void **addr, size_t *length);
int monloop_unmapfile(void *addr, size_t length);

static inline int monloop_settimer(monloop_timer_t *tmr, 
				  long sec, long nsec,
				  long isec, long insec) {
  int rc;
  struct itimerspec new_value;
  new_value.it_value.tv_sec = sec;
  new_value.it_value.tv_nsec = nsec;
  new_value.it_interval.tv_sec = isec;
  new_value.it_interval.tv_nsec = insec;
  rc = timerfd_settime(tmr->fd, 0, &new_value, NULL);
  return rc;
}

static inline int monloop_canceltimer(monloop_timer_t *tmr) {
	return monloop_settimer(tmr, 0, 0, 0, 0);
}

// deal with cleanup and destruciton

// add monloop_stop


#define MONLOOP_CMD_EXIT   -1
#define MONLOOP_CMD_OK      1
#define MONLOOP_CMD_FAILED  0

typedef struct monloop_cmddesc {
  const char *name;
  const char *usage;
  monloop_cmd_t cmd;
} monloop_cmddesc_t;

long monloop_help_cmd(monloop_t *, int args);

extern monloop_cmddesc_t monloop_cmds[];
#ifdef __cplusplus
}
#endif
#endif
