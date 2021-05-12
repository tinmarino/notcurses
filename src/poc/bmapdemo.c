#include <math.h>
#include <wchar.h>
#include <notcurses/notcurses.h>

static int
bmaps(struct notcurses* nc, struct ncvisual** ncvs){
  if(notcurses_check_pixel_support(nc) < 1){
    return -1;
  }
  ncvs[0] = ncvisual_from_file("../data/buzz.png");
  ncvs[1] = ncvisual_from_file("../data/spaceship.png");
  ncvs[2] = ncvisual_from_file("../data/warmech.bmp");
  ncvisual_resize(ncvs[0], 212, 192);
  return 0;
}

static inline uint64_t ts2ns(const struct timespec* ts){
  return ts->tv_sec * 1000000000ull + ts->tv_nsec;
}

static inline int
distance(int i0, int i1){
  return i0 > i1 ? i0 - i1 : i1 - i0;
}

// dimensions are of standard plane
static int
move_buzz(struct ncplane* buzz, int dimy, int dimx){
  static double theta = 0;
  int oy, ox; // origin
  oy = dimy / 2;
  ox = dimx / 2;
  int y, x; // our centroid
  ncplane_yx(buzz, &y, &x);
  y += ncplane_dim_y(buzz) / 2;
  x += ncplane_dim_x(buzz) / 2;
  double dy, dx; // distances
  dy = distance(y, oy);
  dx = distance(x, ox);
  fprintf(stderr, "y: %d x: %d d: %g/%g/%g\n", y, x, dy, dx, dy/dx);
  //double r = sqrt(dy * dy + dx * dx) + (random() % 2);
  double r = 10;
  //double theta = atan(dy / dx);
  theta += M_PI / 4;
  fprintf(stderr, "distance: %g theta: %g\n", r, theta);
  int newy, newx;
  newy = (oy - ncplane_dim_y(buzz) / 2) + sin(theta) * r;
  newx = (ox - ncplane_dim_x(buzz) / 2) + cos(theta) * r;
  fprintf(stderr, "newy: %d newx: %d\n", newy, newx);
  if(ncplane_move_yx(buzz, newy, newx)){
    return -1;
  }
  return 0;
}

static int
demo(struct notcurses* nc){
  struct ncvisual* ncvs[3]; // buzz, ship, hrmm
  if(bmaps(nc, ncvs) < 0){
    return -1;
  }
  int dimy, dimx;
  struct ncplane* stdn = notcurses_stddim_yx(nc, &dimy, &dimx);
  struct ncvisual_options vopts = {
    .y = NCALIGN_CENTER,
    .x = NCALIGN_CENTER,
    .blitter = NCBLIT_PIXEL,
    .flags = NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_HORALIGNED,
  };
  struct ncplane* buzz = ncvisual_render(nc, ncvs[0], &vopts);
  uint64_t nowns, lastns = 0;
  while(true){
    for(int y = 0 ; y < dimy ; ++y){
      for(int x = 0 ; x < dimx ; ++x){
        wchar_t w;
        do{
          w = random() % 0xd7fflu;
        }while(wcwidth(w) != 1 || iswspace(w));
        ncplane_putwc_yx(stdn, y, x, w);
      }
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    nowns = ts2ns(&now);
    // move buzz along a spiral, 20 moves per second
    if(nowns - lastns > 50000000ull){
      move_buzz(buzz, dimy, dimx);
      lastns = nowns;
    }
    notcurses_render(nc);
    struct timespec bsts = { 0, 100000000, };
    clock_nanosleep(CLOCK_MONOTONIC, 0, &bsts, NULL);
  }
  return 0;
}

int main(void){
  srand(time(NULL));
  struct notcurses* nc = notcurses_init(NULL, NULL);
  int r = demo(nc);
  notcurses_stop(nc);
  return r;
}
