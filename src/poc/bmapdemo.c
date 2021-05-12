#include <math.h>
#include <wchar.h>
#include <notcurses/notcurses.h>

static int
bmaps(struct notcurses* nc, struct ncvisual** ncvs){
  if(notcurses_check_pixel_support(nc) < 1){
    return -1;
  }
  ncvs[0] = ncvisual_from_file("../data/buzz.png");
  ncvs[1] = ncvisual_from_file("../data/von1.png");
  ncvs[2] = ncvisual_from_file("../data/von2.png");
  ncvs[3] = ncvisual_from_file("../data/von3.png");
  ncvs[4] = ncvisual_from_file("../data/von4.png");
  ncvisual_resize(ncvs[0], 212, 192);
  return 0;
}

static inline uint64_t ts2ns(const struct timespec* ts){
  return ts->tv_sec * 1000000000ull + ts->tv_nsec;
}

// swap out the character background on the standard plane
static int
freak(struct ncplane* stdn, int dimy, int dimx){
  int r = random() % 128 + 127;
  int g = random() % 128 + 127;
  int b = random() % 128 + 127;
  ncplane_set_fg_rgb8(stdn, r, g, b);
  for(int y = 0 ; y < dimy ; ++y){
    for(int x = 0 ; x < dimx ; ++x){
      wchar_t w;
      do{
        w = random() % 0xd7fflu;
      }while(wcwidth(w) != 1 || iswspace(w));
      ncplane_putwc_yx(stdn, y, x, w);
    }
  }
  return 0;
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
  /*
  double dy, dx; // distances
  dy = distance(y, oy);
  dx = distance(x, ox);*/
  //fprintf(stderr, "y: %d x: %d d: %g/%g/%g\n", y, x, dy, dx, dy/dx);
  //double r = sqrt(dy * dy + dx * dx) + (random() % 2);
  double r = 10;
  //double theta = atan(dy / dx);
  theta += M_PI / 8;
  //fprintf(stderr, "distance: %g theta: %g\n", r, theta);
  int newy, newx;
  newy = (oy - ncplane_dim_y(buzz) / 2) + sin(theta) * r;
  newx = (ox - ncplane_dim_x(buzz) / 2) + cos(theta) * r;
  //fprintf(stderr, "newy: %d newx: %d\n", newy, newx);
  if(ncplane_move_yx(buzz, newy, newx)){
    return -1;
  }
  return 0;
}

// cycle the standard plane between von moves
static int
von_freak_cycle(struct notcurses* nc, struct ncplane* stdn, int dimy, int dimx){
  struct timespec bsts = { 0, 10000000, };
  for(int i = 0 ; i < 10 ; ++i){
    freak(stdn, dimy, dimx);
    notcurses_render(nc);
    clock_nanosleep(CLOCK_MONOTONIC, 0, &bsts, NULL);
  }
  return 0;
}

// four of 'em
static int
demo_von(struct notcurses* nc, struct ncvisual** vons){
  struct ncvisual_options vopts = {
    .y = NCALIGN_CENTER,
    .x = NCALIGN_CENTER,
    .blitter = NCBLIT_PIXEL,
    .flags = NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_HORALIGNED,
  };
  int dimy, dimx;
  struct ncplane* stdn = notcurses_stddim_yx(nc, &dimy, &dimx);
  int px, py, cdimy, cdimx;
  ncplane_pixelgeom(notcurses_stdplane(nc), &py, &px, &cdimy, &cdimx, NULL, NULL);
  int ylen, xlen;
  ncvisual_blitter_geom(nc, vons[3], &vopts, &ylen, &xlen, NULL, NULL, NULL);
fprintf(stderr, "p: %d/%d c: %d/%d l: %d/%d\n", py, px, cdimy, cdimx, ylen, xlen);
  int by, bx; // number of columns/rows taken by big one (lower-right)
  by = (ylen + (cdimy - 1)) / cdimy;
  bx = (xlen + (cdimx - 1)) / cdimx;
  struct ncplane *vplanes[4];
  vplanes[3] = ncvisual_render(nc, vons[3], &vopts);
  ncplane_move_yx(vplanes[3], (dimy - by) / 2, (dimx - bx) / 2);
  notcurses_render(nc);
  vplanes[0] = ncvisual_render(nc, vons[0], &vopts);
  int y, x;
  ncplane_yx(vplanes[0], &y, &x);
  while(y > (dimy - by) / 2){
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    --y;
    ncplane_move_yx(vplanes[0], y, x);
  }
  while(x > (dimx - bx) / 2){
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    --x;
    ncplane_move_yx(vplanes[0], y, x);
  }
  vplanes[1] = ncvisual_render(nc, vons[1], &vopts);
  ncplane_yx(vplanes[1], &y, &x);
  while(y > (dimy - by) / 2){
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    --y;
    ncplane_move_yx(vplanes[1], y, x);
  }
  while(x < dimx / 2){
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    ++x;
    ncplane_move_yx(vplanes[1], y, x);
  }
  vplanes[2] = ncvisual_render(nc, vons[2], &vopts);
  ncplane_yx(vplanes[2], &y, &x);
  while(y < dimy / 2){
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    ++y;
    ncplane_move_yx(vplanes[2], y, x);
  }
  while(x > (dimx - bx) / 2){
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    --x;
    ncplane_move_yx(vplanes[2], y, x);
  }
  return 0;
}

static int
demo(struct notcurses* nc){
  struct ncvisual* ncvs[5]; // buzz, von neumann, ...
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
  uint64_t startns = 0, nowns, lastns = 0;
  while(true){
    freak(stdn, dimy, dimx);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    nowns = ts2ns(&now);
    // move buzz along a spiral, 20 moves per second
    if(nowns - lastns > 50000000ull){
      move_buzz(buzz, dimy, dimx);
      if(lastns == 0){
        startns = nowns;
      }
      lastns = nowns;
    }
    notcurses_render(nc);
    if(nowns - startns > 500000000ull){
      break;
    }
    struct timespec bsts = { 0, 10000000, };
    clock_nanosleep(CLOCK_MONOTONIC, 0, &bsts, NULL);
  }
  ncvisual_destroy(ncvs[0]);
  ncplane_destroy(buzz);
  notcurses_render(nc);
  if(demo_von(nc, ncvs + 1)){
    return -1;
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
