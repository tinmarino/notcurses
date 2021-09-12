#include <stdio.h>
#include "internal.h"
#include "in.h"

// Notcurses takes over stdin, and if it is not connected to a terminal, also
// tries to make a connection to the controlling terminal. If such a connection
// is made, it will read from that source (in addition to stdin). We dump one or
// both into distinct buffers. We then try to lex structured elements out of
// the buffer(s). We can extract cursor location reports, mouse events, and
// UTF-8 characters. Completely extracted ones are placed in their appropriate
// queues, and removed from the depository buffer. We aim to consume the
// entirety of the deposit before going back to read more data, but let anyone
// blocking on data wake up as soon as we've processed any input.
//
// The primary goal is to react to terminal messages (mostly cursor location
// reports) as quickly as possible, and definitely not with unbounded latency,
// without unbounded allocation, and also without losing data. We'd furthermore
// like to reliably differentiate escapes and regular input, even when that
// latter contains escapes. Unbounded input will hopefully only be present when
// redirected from a file (NCOPTION_TOSS_INPUT)

static sig_atomic_t resize_seen;

// called for SIGWINCH and SIGCONT
void sigwinch_handler(int signo){
  resize_seen = signo;
}

// data collected from responses to our terminal queries.
typedef struct termqueries {
  int celly, cellx;     // cell geometry on startup
  int pixy, pixx;       // pixel geometry on startup
  int cursory, cursorx; // cursor location on startup
  unsigned kittygraphs; // are kitty graphics supported?
  int sixely, sixelx;   // maximum sixel size
  int cregs;            // sixel color registers
  unsigned appsync;     // application-sync supported?
} termqueries;

typedef struct cursorloc {
  int y, x;             // 0-indexed cursor location
} cursorloc;

// local state for the input thread. don't put this large struct on the stack.
typedef struct inputctx {
  int stdinfd;          // bulk in fd. always >= 0 (almost always 0). we do not
                        //  own this descriptor, and must not close() it.
  int termfd;           // terminal fd: -1 with no controlling terminal, or
                        //  if stdin is a terminal, or on MSFT Terminal.
#ifdef __MINGW64__
  HANDLE stdinhandle;   // handle to input terminal for MSFT Terminal
#endif

  // these two are not ringbuffers; we always move any leftover materia to the
  // front of the queue (it ought be a handful of bytes at most).
  unsigned char ibuf[BUFSIZ]; // might be intermingled bulk/control data
  unsigned char tbuf[BUFSIZ]; // only used if we have distinct terminal fd
  int ibufvalid;      // we mustn't read() if ibufvalid == sizeof(ibuf)
  int tbufvalid;      // only used if we have distinct terminal connection

  // ringbuffers for processed, structured input
  cursorloc* csrs;    // cursor reports are dumped here
  ncinput* inputs;    // processed input is dumped here
  int csize, isize;   // total number of slots in csrs/inputs
  int cvalid, ivalid; // population count of csrs/inputs
  int cwrite, iwrite; // slot where we'll write the next csr/input;
                      //  we cannot write if valid == size
  int cread, iread;   // slot from which clients read the next csr/input;
                      //  they cannot read if valid == 0
  pthread_mutex_t ilock; // lock for ncinput ringbuffer
  pthread_cond_t icond;  // condvar for ncinput ringbuffer
  tinfo* ti;          // link back to tinfo
  pthread_t tid;      // tid for input thread
} inputctx;

static inline inputctx*
create_inputctx(tinfo* ti, FILE* infp){
  inputctx* i = malloc(sizeof(*i));
  if(i){
    i->csize = 64;
    if( (i->csrs = malloc(sizeof(*i->csrs) * i->csize)) ){
      i->isize = BUFSIZ;
      if( (i->inputs = malloc(sizeof(*i->inputs) * i->isize)) ){
        if(pthread_mutex_init(&i->ilock, NULL) == 0){
          if(pthread_cond_init(&i->icond, NULL) == 0){
            if((i->stdinfd = fileno(infp)) >= 0){
              if(set_fd_nonblocking(i->stdinfd, 1, &ti->stdio_blocking_save) == 0){
                i->termfd = tty_check(i->stdinfd) ? -1 : get_tty_fd(infp);
                i->ti = ti;
                i->cvalid = i->ivalid = 0;
                i->cwrite = i->iwrite = 0;
                i->cread = i->iread = 0;
                i->ibufvalid = 0;
                logdebug("input descriptors: %d/%d\n", i->stdinfd, i->termfd);
                return i;
              }
            }
            pthread_cond_destroy(&i->icond);
          }
          pthread_mutex_destroy(&i->ilock);
        }
        free(i->inputs);
      }
      free(i->csrs);
    }
    free(i);
  }
  return NULL;
}

static inline void
free_inputctx(inputctx* i){
  if(i){
    // we *do not* own stdinfd; don't close() it! we do own termfd.
    if(i->termfd >= 0){
      close(i->termfd);
    }
    pthread_mutex_destroy(&i->ilock);
    pthread_cond_destroy(&i->icond);
    // do not kill the thread here, either.
    free(i->inputs);
    free(i->csrs);
    free(i);
  }
}

// populate |buf| with any new data from the specified file descriptor |fd|.
//
static void
read_input_nblock(int fd, unsigned char* buf, size_t buflen, int *bufused){
  if(fd < 0){
    return;
  }
  size_t space = buflen - *bufused;
  if(space == 0){
    return;
  }
  ssize_t r = read(fd, buf + *bufused, space);
  if(r <= 0){
    // FIXME diagnostic on permanent failures
    return;
  }
  *bufused += r;
  space -= r;
  loginfo("read %lldB from %d (%lluB left)\n", (long long)r, fd, (unsigned long long)space);
}

// are terminal and stdin distinct for this inputctx?
static inline bool
ictx_independent_p(const inputctx* ictx){
  return ictx->termfd >= 0; // FIXME does this hold on MSFT Terminal?
}

// process as many control sequences from |buf|, having |bufused| bytes,
// as we can. anything not a valid control sequence is dropped. this text
// needn't be valid UTF-8.
static void
process_escapes(inputctx* ictx, unsigned char* buf, int* bufused){
}

// process as much bulk UTF-8 input as we can, knowing it to be free of control
// sequences. anything not a valid UTF-8 character is dropped. a control
// sequence will be chopped up and passed up (assuming it to be valid UTF-8).
static void
process_bulk(inputctx* ictx, unsigned char* buf, int* bufused){
}

// process as much mixed input as we can. we might find UTF-8 bulk input and
// control sequences mixed (though each individual character/sequence ought be
// contiguous). known control sequences are removed for internal processing.
// everything else will be handed up to the client (assuming it to be valid
// UTF-8).
static void
process_melange(inputctx* ictx, unsigned char* buf, int* bufused){
}

// walk the matching automaton from wherever we were.
static void
process_ibuf(inputctx* ictx){
  if(ictx->tbufvalid){
    // we could theoretically do this in parallel with process_bulk, but it
    // hardly seems worthwhile without breaking apart the fetches of input.
    process_escapes(ictx, ictx->tbuf, &ictx->tbufvalid);
  }
  if(ictx->ibufvalid){
    if(ictx_independent_p(ictx)){
      process_bulk(ictx, ictx->ibuf, &ictx->ibufvalid);
    }else{
      process_melange(ictx, ictx->ibuf, &ictx->ibufvalid);
    }
  }
}

// populate the ibuf with any new data, up through its size, but do not block.
// don't loop around this call without some kind of readiness notification.
static void
read_inputs_nblock(inputctx* ictx){
  // first we read from the terminal, if that's a distinct source.
  read_input_nblock(ictx->termfd, ictx->tbuf, sizeof(ictx->tbuf),
                    &ictx->tbufvalid);
  // now read bulk, possibly with term escapes intermingled within (if there
  // was not a distinct terminal source).
  read_input_nblock(ictx->stdinfd, ictx->ibuf, sizeof(ictx->ibuf),
                    &ictx->ibufvalid);
}

static void*
input_thread(void* vmarshall){
  inputctx* ictx = vmarshall;
  for(;;){
    read_inputs_nblock(ictx);
    // process anything we've read
    process_ibuf(ictx);
  }
  return NULL;
}

int init_inputlayer(tinfo* ti, FILE* infp){
  inputctx* ictx = create_inputctx(ti, infp);
  if(ictx == NULL){
    return -1;
  }
  if(pthread_create(&ictx->tid, NULL, input_thread, ictx)){
    free_inputctx(ictx);
    return -1;
  }
  ti->ictx = ictx;
  loginfo("spun up input thread\n");
  return 0;
}

int stop_inputlayer(tinfo* ti){
  int ret = 0;
  if(ti){
    if(ti->ictx){
      loginfo("tearing down input thread\n");
      ret |= cancel_and_join("input", ti->ictx->tid, NULL);
      ret |= set_fd_nonblocking(ti->ictx->stdinfd, ti->stdio_blocking_save, NULL);
      free_inputctx(ti->ictx);
      ti->ictx = NULL;
    }
  }
  return ret;
}

int inputready_fd(const inputctx* ictx){
  return ictx->stdinfd;
}

static inline uint32_t
internal_get(inputctx* ictx, const struct timespec* ts, ncinput* ni,
             int lmargin, int tmargin){
  pthread_mutex_lock(&ictx->ilock);
  while(!ictx->ivalid){
    pthread_cond_wait(&ictx->icond, &ictx->ilock);
  }
  memcpy(ni, &ictx->inputs[ictx->iread], sizeof(*ni));
  if(++ictx->iread == ictx->isize){
    ictx->iread = 0;
  }
  pthread_mutex_unlock(&ictx->ilock);
  // FIXME adjust mouse coordinates for margins
  return ni->id;
  /*
  uint32_t r = ncinputlayer_prestamp(&nc->tcache, ts, ni,
                                     nc->margin_l, nc->margin_t);
  if(r != (uint32_t)-1){
    uint64_t stamp = nc->tcache.input.input_events++; // need increment even if !ni
    if(ni){
      ni->seqnum = stamp;
    }
  }
  return r;
  */
}

// infp has already been set non-blocking
uint32_t notcurses_get(notcurses* nc, const struct timespec* ts, ncinput* ni){
  uint32_t r = internal_get(nc->tcache.ictx, ts, ni,
                            nc->margin_l, nc->margin_t);
  if(r != (uint32_t)-1){
    ++nc->stats.s.input_events;
  }
  return r;
}

uint32_t ncdirect_get(ncdirect* n, const struct timespec* ts, ncinput* ni){
  return internal_get(n->tcache.ictx, ts, ni, 0, 0);
}

__attribute__ ((visibility("default")))
uint32_t notcurses_getc(notcurses* nc, const struct timespec* ts,
                        const void* unused, ncinput* ni){
  (void)unused; // FIXME remove for abi3
  return notcurses_get(nc, ts, ni);
}

__attribute__ ((visibility("default")))
uint32_t ncdirect_getc(ncdirect* nc, const struct timespec *ts,
                       const void* unused, ncinput* ni){
  (void)unused; // FIXME remove for abi3
  return ncdirect_get(nc, ts, ni);
}
