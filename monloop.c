#include "monloop.h"
#include <assert.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

// incase we want to turn asserts off or interpose on them
#define ASSERT(...) assert(__VA_ARGS__)

static bool checkfd(int fd)
{
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    fprintf(stderr, "fstat on %d failed errno=%d\n", fd, errno);
    if (errno == EBADFD) {
      fprintf(stderr, "EBADFD: %d\n", fd);
    }
    return false;
  }
  return true;
}

void monloop_greeting(monloop_t *this) { ML_PRINT(this, "%s", "> "); }

static void
monloop_usage(monloop_t *this)
{
  for (int i=0; monloop_cmds[i].cmd != NULL; i++) {
    monloop_cmddesc_t *cmddesc = &(monloop_cmds[i]);
    if (cmddesc->usage != NULL) {
      ML_PRINT(this,"\t'%s'\t%s\n", cmddesc->name, cmddesc->usage);
    }
  }
}

long
monloop_help_cmd(monloop_t *this, int args)
{
  monloop_usage(this);
  return MONLOOP_CMD_OK;
}

int
monloop_mapfile(const char *filename, void **addr, size_t *length)
{
  void *map;
  struct stat sb;
  int fd = open(filename, O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    perror("open");
    return -1;
  }
 
  if (fstat(fd, &sb) == -1) {
    perror("fstat");
    close(fd);
    return -1;
  }

  map = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return -1;
  }
  close(fd);
  *addr = map;
  *length = sb.st_size;
  return 0;
}

int monloop_unmapfile(void *addr, size_t length)
{
  int rc = munmap(addr, length);
  if (rc == -1) {
    perror("munmap");
    return -1;
  }
  return 0;
}

int
monloop_removetimer(monloop_t *this, monloop_timer_t *timer)
{
  monloop_timer_t *tmr = this->timers;
  ASSERT(timer && tmr);
  
  if (tmr == timer) {
    // head of list
    this->timers = tmr->next;
  } else {
    // search the list
    for (; tmr != NULL; tmr = tmr->next) {
      if (tmr->next == timer) {
	tmr->next = timer->next;
	break;
      }
    }
    ASSERT(tmr != NULL);
  }
  if (timer) { 
    epoll_ctl(this->epollfd, EPOLL_CTL_DEL, timer->fd, NULL);
    close(timer->fd);
    free(timer);
    return 1;
  }
  return 0;
}

monloop_timer_t *
monloop_addtimer(monloop_t *this, int clockid, monloop_timer_func_t tfunc)
{
  int tfd;
  monloop_timer_t *timer = (monloop_timer_t *)malloc(sizeof(monloop_timer_t));
  ASSERT(timer != NULL);
  timer->func    = tfunc;
  timer->monloop = this;
  timer->next    = this->timers;
  this->timers   = timer;

  tfd = timerfd_create(clockid, TFD_CLOEXEC | TFD_NONBLOCK);
  ASSERT(tfd != -1);
  timer->fd = tfd;
  {
      struct epoll_event ev;
      int epollfd = this->epollfd;
      int fd = tfd;
      ev.events   = EPOLLIN;
      ev.data.ptr = timer;
      if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
	perror("epoll_ctl: fd");
	goto error;
      }
  } 
  return timer;
 error:
  monloop_removetimer(this, timer);
  return NULL;
}

static long
monProcess(monloop_t *this)
{
  char *cmd = this->line;
  int   n    = this->n;
  int   i, j, args=0;
  
  if (n<=0) return MONLOOP_CMD_FAILED;
  for (i=0; i<sizeof(this->line); i++) {
    if (cmd[i] == ' ') {
      cmd[i]='\0';
      args=i+1;
      break;
    }
    if (cmd[i]=='\0') break;
  }

  // we did not find a null or new line??
  if (cmd[i]!=0) {
    ML_PRINT(this, "mon: invalid cmd line: i=%d\n",i);
    return MONLOOP_CMD_FAILED;
  }
  
  // check for command and execute 
  for (j=0; monloop_cmds[j].cmd != NULL; j++) {
    if (strncmp(cmd, monloop_cmds[j].name, i+1)==0) {
      monloop_cmd_t cmdfunc = monloop_cmds[j].cmd;
      int rc = cmdfunc(this, args); 
      if (rc>=MONLOOP_CMD_OK)     { ML_PRINT(this, "%s", "OK\n"); }
      if (rc==MONLOOP_CMD_FAILED) { ML_PRINT(this, "%s", "FAILED\n"); }
      if (rc!=MONLOOP_CMD_EXIT)   { monloop_greeting(this); }
      return rc;
    }
  }
  
  ML_PRINT(this, "%s: command not found\n", cmd);
  monloop_usage(this);
  ML_PRINT(this, "%s", "FAILED\n");
  monloop_greeting(this);
  return MONLOOP_CMD_FAILED;
}

static long handle_event(monloop_t *this, uint32_t evnts)
{
  long rc=0;
  
  if (evnts & EPOLLIN) {
    int curn = this->n;
    // leave room for null termination
    ASSERT(curn<sizeof(this->line));
    int n = read(this->fd, &this->line[curn], 1);
    assert(n==1);
    curn+=n;
    assert(curn <= sizeof(this->line));
    
    if (curn == (sizeof(this->line)-1) && this->line[curn]!='\n') {
      fprintf(stderr, "exceeded monloop line buffer length=%ld...ignoring\n",
	      sizeof(this->line));
      this->n = 0;
    } else {
      this->n = curn;
      if (this->line[curn-1]=='\n') {
	this->line[curn-1]='\0';
	rc = monProcess(this);
	this->n = 0;
      }
    }
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    fprintf(stderr,"EPOLLHUP(%x)\n", EPOLLHUP);
    evnts = evnts & ~EPOLLHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLRDHUP) {
    fprintf(stderr,"EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    evnts = evnts & ~EPOLLRDHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLERR) {
    fprintf(stderr,"EPOLLERR(%x)\n", EPOLLERR);
    evnts = evnts & ~EPOLLRDHUP;
    if (evnts==0) goto done;
  }
  if (evnts != 0) {
    fprintf(stderr,"unknown events evnts:%x", evnts);
  }
 done:
  return rc;
}

#define MAX_EVENTS 1024
static void *theLoop(void *arg)
{
  monloop_t *this = arg;
  int epollfd;
  struct epoll_event events[MAX_EVENTS];
  long rc = 0;
  
  // create the kernel event poll object
  {
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
      perror("epoll_create1");
      return NULL;
    }
  }
  this->epollfd = epollfd;
  
  // register for the monitor interface events
  {
      struct epoll_event ev;
      int fd = this->fd;
      ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR; // Level 
      ev.data.ptr = this;
      if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
	perror("epoll_ctl: fd");
	return NULL;
      }
  }

  pthread_barrier_wait(&(this->barrier));
  
  monloop_greeting(this);
  
  for (;;) {
    errno = 0;
    int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    // ERROR Handling
    if (nfds == -1) {
      perror("epoll_wait");
      if (errno == EINTR) {
	// maybe we got a signal we are not handling or something
	// else made us wakeup ...  log it but just keep on going 
	fprintf(stderr, "%s: EINTR: Continuing\n", __func__);
	continue;
      }
      if (errno == EINVAL) {
	fprintf(stderr, "FAIL: EINVAL: epollfd=%d ME=%d checkfd=%d\n",
	        epollfd, MAX_EVENTS, checkfd(epollfd));
	continue;
      }
      rc = false;
      fprintf(stderr, "FAIL:errno=%d epollfd=%d ME=%d checkfd=%d\n",
	      errno, epollfd, MAX_EVENTS, checkfd(epollfd));
      goto done;
    }
    // Event Handling
    for (int n = 0; n < nfds; ++n) {
      uint32_t evnts = events[n].events;
      if (this == events[n].data.ptr) {
	// monitor command event
	rc = handle_event(this, evnts);
	if (rc <= MONLOOP_CMD_EXIT) goto done;
      } else {
	// timer event
	monloop_timer_t *timer = (monloop_timer_t *)events[n].data.ptr;
	ASSERT(timer->monloop == this);
	rc = timer->func(timer, evnts);
	if (rc <= MONLOOP_CMD_EXIT) goto done;
      }
    }
  }
 done:
  return (void *)rc;
}

static void
monloop_init(monloop_t *this, void *data, int fd, FILE *fp, bool silent) 
{
  int rc;
  
  this->line[0] = 0;
  this->data    = data;
  this->fd      = fd;
  this->fp      = (fp) ? fp : stderr;
  this->n       = 0;
  this->silent  = silent;
  this->running = false;

  rc = pthread_barrier_init(&(this->barrier), NULL, 2);
  ASSERT(rc == 0);
  
  rc = pthread_create(&this->tid, NULL, &theLoop, this);
  ASSERT(rc==0);
  this->running = true;                    // joinable at this point
  
  pthread_barrier_wait(&(this->barrier)); 
}

int
monloop_start(monloop_t *this, void *data, int fd, FILE *fp, bool silent) 
{
  monloop_init(this, data, fd, fp, silent);
  return 1; 
}

int
monloop_join(monloop_t *this, void **exitstatus)
{
  if (this->running) return pthread_join(this->tid, exitstatus);
  return 0;
}

