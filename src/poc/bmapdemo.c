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
  ncvs[5] = ncvisual_from_file("../data/ulam.png");
  ncvs[6] = ncvisual_from_file("../data/eatme.png");
  ncvs[7] = ncvisual_from_file("../data/drinkme.png");
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
        w = random() % 0x8fflu;
      }while(wcwidth(w) != 1 || iswspace(w) || iswpunct(w));
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

static int
demo_ulam(struct notcurses* nc, struct ncvisual* ulam){
  int dimy, dimx;
  struct ncplane* stdn = notcurses_stddim_yx(nc, &dimy, &dimx);
  struct ncvisual_options vopts = {
    .y = NCALIGN_CENTER,
    .x = NCALIGN_CENTER,
    .blitter = NCBLIT_PIXEL,
    .flags = NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_HORALIGNED,
  };
  struct ncplane* u = ncvisual_render(nc, ulam, &vopts);
  struct ncplane_options opts = {
    .rows = 6,
    .cols = 55,
    .y = 33,
    .x = 3,
  };
  struct ncplane* l = ncplane_create(u, &opts);
  if(l == NULL){
    return -1;
  }
  ncplane_set_fg_rgb(l, 0xdd2222);
  ncplane_putstr_yx(l, 0, 0, "CENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSORE");
  ncplane_putstr_yx(l, 1, 0, "DCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOR");
  ncplane_putstr_yx(l, 2, 0, "EDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSO");
  ncplane_putstr_yx(l, 3, 0, "REDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENS");
  ncplane_putstr_yx(l, 4, 0, "OREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCEN");
  ncplane_putstr_yx(l, 5, 0, "SOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCENSOREDCE");
  ncplane_set_fg_rgb(l, 0x22dd22);
  ncplane_set_styles(l, NCSTYLE_ITALIC);
  ncplane_putstr_aligned(l, 2, NCALIGN_CENTER, " (for your ignorance) ");
  for(int i = 0 ; i < 50 ; ++i){
    von_freak_cycle(nc, stdn, dimy, dimx);
  }
  notcurses_render(nc);
  ncplane_destroy(l);
  ncplane_destroy(u);
  return 0;
}

static int
demo_shrink(struct notcurses* nc, struct ncvisual* me){
  int dimy, dimx;
  struct ncplane* stdn = notcurses_stddim_yx(nc, &dimy, &dimx);
  int py, px;
  struct ncvisual_options vopts = {
    .y = NCALIGN_CENTER,
    .x = NCALIGN_CENTER,
    .blitter = NCBLIT_PIXEL,
    .flags = NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_HORALIGNED,
  };
  int cdimy, cdimx;
  ncvisual_blitter_geom(nc, me, &vopts, &py, &px, &cdimy, &cdimx, NULL);
  struct ncplane* p = NULL;
  if(py % cdimy){
    py -= (py % cdimy);
  }
  if(px % cdimx){
    px -= (px % cdimx);
  }
  while(py > cdimy && px > cdimx){
    if(py > cdimy){
      py -= cdimy / 2;
    }
    if(px > cdimx){
      px -= cdimx / 2;
    }
    freak(stdn, dimy, dimx);
    ncvisual_resize(me, py, px);
    p = ncvisual_render(nc, me, &vopts);
    notcurses_render(nc);
    ncplane_destroy(p);
  }
  return 0;
}

static int
demo_grow(struct notcurses* nc, struct ncvisual* me){
  int dimy, dimx;
  struct ncplane* stdn = notcurses_stddim_yx(nc, &dimy, &dimx);
  int py, px;
  struct ncvisual_options vopts = {
    .y = NCALIGN_CENTER,
    .x = NCALIGN_CENTER,
    .blitter = NCBLIT_PIXEL,
    .flags = NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_HORALIGNED,
  };
  int cdimy, cdimx;
  ncvisual_blitter_geom(nc, me, &vopts, &py, &px, &cdimy, &cdimx, NULL);
  py = 0;
  px = 0;
  struct ncplane* p = NULL;
  while(py < 500 && px < 240){
    if(py < cdimy * (dimy - 1)){
      py += cdimy / 2;
    }
    if(px < cdimx * dimx){
      px += cdimx / 2;
    }
    if(me == NULL){
      me = ncvisual_from_file("../data/drinkme.png");
    }
    ncvisual_resize(me, py, px);
    p = ncvisual_render(nc, me, &vopts);
    notcurses_render(nc);
    von_freak_cycle(nc, stdn, dimy, dimx);
    ncplane_destroy(p);
    ncvisual_destroy(me);
    me = NULL;
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
  struct ncplane_options opts = {
    .rows = 3,
    .cols = 80,
    .y = 11,
  };
  struct ncplane* l = ncplane_create(stdn, &opts);
  ncplane_set_fg_rgb(l, 0xeeeeee);
  ncplane_set_styles(l, NCSTYLE_BOLD);
  ncplane_putstr_aligned(l, 0, NCALIGN_CENTER, "\"doyouknowhatiamsaying, robert?\"");
  ncplane_putstr_aligned(l, 2, NCALIGN_CENTER, "\"yesiknowhatyouaresaying, john.\"");
  ncplane_set_base(l, " ", 0, 0);
  opts.y = 43;
  struct ncplane* ll = ncplane_create(stdn, &opts);
  ncplane_set_fg_rgb(ll, 0xeeeeee);
  ncplane_set_styles(ll, NCSTYLE_BOLD);
  ncplane_putstr_aligned(ll, 0, NCALIGN_CENTER, "[pause] \"i honestly kinda doubt that, robert.\"");
  ncplane_putstr_aligned(ll, 2, NCALIGN_CENTER, "\"fuck you, john.\"");
  ncplane_set_base(ll, " ", 0, 0);
  int px, py, cdimy, cdimx;
  ncplane_pixelgeom(notcurses_stdplane(nc), &py, &px, &cdimy, &cdimx, NULL, NULL);
  int ylen, xlen;
  ncvisual_blitter_geom(nc, vons[3], &vopts, &ylen, &xlen, NULL, NULL, NULL);
//fprintf(stderr, "p: %d/%d c: %d/%d l: %d/%d\n", py, px, cdimy, cdimx, ylen, xlen);
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
  for(int i = 0 ; i < 4 ; ++i){
    ncplane_destroy(vplanes[i]);
  }
  ncplane_destroy(ll);
  ncplane_destroy(l);
  return 0;
}

static int
demo(struct notcurses* nc){
  struct ncvisual* ncvs[8]; // buzz, von neumann, ulam, eatme, drinkme...
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
  struct ncplane_options opts = {
    .rows = 3,
    .cols = dimx,
    .y = 3,
  };
  struct ncplane* l = ncplane_create(stdn, &opts);
  ncplane_set_fg_rgb(l, 0xffd700);
  ncplane_set_base(l, " ", 0, 0);
  ncplane_putstr_aligned(l, 0, NCALIGN_CENTER, "GT Nuclear Engineering, home of the three-eyed Buzz");
  ncplane_set_fg_rgb(l, 0xdddddd);
  ncplane_putstr_aligned(l, 2, NCALIGN_CENTER, "(but still no phd 😞)");
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
    if(nowns - startns > 5000000000ull){
      break;
    }
    struct timespec bsts = { 0, 10000000, };
    clock_nanosleep(CLOCK_MONOTONIC, 0, &bsts, NULL);
  }
  ncvisual_destroy(ncvs[0]);
  ncplane_destroy(buzz);
  ncplane_destroy(l);
  notcurses_render(nc);
  if(demo_von(nc, ncvs + 1)){
    return -1;
  }
  if(demo_ulam(nc, ncvs[5])){
    return -1;
  }
  if(demo_shrink(nc, ncvs[6])){
    return -1;
  }
  if(demo_grow(nc, ncvs[7])){
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
