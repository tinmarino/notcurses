#ifndef NOTCURSES_IN
#define NOTCURSES_IN

#ifdef __cplusplus
extern "C" {
#endif

// internal header, not installed

#include <stdio.h>

struct tinfo;
struct inputctx;

int init_inputlayer(struct tinfo* ti, FILE* infp)
  __attribute__ ((nonnull (1, 2)));

int stop_inputlayer(struct tinfo* ti);

int inputready_fd(const struct inputctx* ictx)
  __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
