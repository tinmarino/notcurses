#include "linux.h"
#include "internal.h"

// auxvecs for framebuffer are 1B each for s->cellpxx * s->cellpxy elements,
// and store the original alpha value.
int fbcon_wipe(sprixel* s, int ycell, int xcell){
  uint8_t* auxvec = sprixel_auxiliary_vector(s);
  if(auxvec == NULL){
    return -1;
  }
  char* glyph = s->glyph.buf;
  for(int y = 0 ; y < s->cellpxy ; ++y){
    if(ycell * s->cellpxy + y >= s->pixy){
      break;
    }
    // number of pixels total above our pixel row
    const size_t yoff = s->pixx * (ycell * s->cellpxy + y);
    for(int x = 0 ; x < s->cellpxx ; ++x){
      if(xcell * s->cellpxx + x >= s->pixx){
        break;
      }
      size_t offset = (yoff + xcell * s->cellpxx + x) * 4;
      const int vyx = (y % s->cellpxy) * s->cellpxx + x;
      auxvec[vyx] = glyph[offset + 3];
      glyph[offset + 3] = 0;
    }
  }
  s->n->tam[s->dimx * ycell + xcell].auxvector = auxvec;
  return 0;
}

int fbcon_blit(struct ncplane* n, int linesize, const void* data,
               int leny, int lenx, const struct blitterargs* bargs){
  uint32_t transcolor = bargs->transcolor;
  int cols = bargs->u.pixel.spx->dimx;
  int rows = bargs->u.pixel.spx->dimy;
  sprixel* s = bargs->u.pixel.spx;
  int cdimx = s->cellpxx;
  int cdimy = s->cellpxy;
  // FIXME this will need be a copy of the tinfo's fbuf map
  size_t flen = leny * lenx * 4;
  if(fbuf_reserve(&s->glyph, flen)){
    return -1;
  }
  tament* tam = NULL;
  bool reuse = false;
  if(n->tam){
    if(n->leny == rows && n->lenx == cols){
      tam = n->tam;
      reuse = true;
    }
  }
  if(!reuse){
    tam = malloc(sizeof(*tam) * rows * cols);
    if(tam == NULL){
      goto error;
    }
    memset(tam, 0, sizeof(*tam) * rows * cols);
  }
  for(int l = 0 ; l < leny ; ++l){
    int ycell = l / cdimy;
    size_t soffset = l * linesize;
    const uint8_t* src = (const unsigned char*)data + soffset;
    size_t toffset = l * lenx * 4;
    char* dst = (char *)s->glyph.buf + toffset;
    for(int c = 0 ; c < lenx ; ++c){
      int xcell = c / cdimx;
      int tyx = xcell + ycell * bargs->u.pixel.spx->dimx;
      if(tam[tyx].state >= SPRIXCELL_ANNIHILATED){
        if(rgba_trans_p(*(uint32_t*)src, transcolor)){
          ncpixel_set_a((uint32_t*)src, 0); // in case it was transcolor
          if(c % cdimx == 0 && l % cdimy == 0){
            tam[tyx].state = SPRIXCELL_ANNIHILATED_TRANS;
          }
        }else{
          tam[tyx].state = SPRIXCELL_ANNIHILATED;
        }
        dst[3] = 0;
        const int vyx = (l % cdimy) * cdimx + (c % cdimx);
        tam[tyx].auxvector[vyx] = src[3];
      }else{
        if(rgba_trans_p(*(uint32_t*)src, transcolor)){
          ncpixel_set_a((uint32_t*)src, 0); // in case it was transcolor
          if(c % cdimx == 0 && l % cdimy == 0){
            tam[tyx].state = SPRIXCELL_TRANSPARENT;
          }else if(tam[tyx].state == SPRIXCELL_OPAQUE_SIXEL){
            tam[tyx].state = SPRIXCELL_MIXED_SIXEL;
          }
          dst[3] = 0;
        }else{
          if(c % cdimx == 0 && l % cdimy == 0){
            tam[tyx].state = SPRIXCELL_OPAQUE_SIXEL;
          }else if(tam[tyx].state == SPRIXCELL_TRANSPARENT){
            tam[tyx].state = SPRIXCELL_MIXED_SIXEL;
          }
          memcpy(dst + 3, src + 3, 1);
        }
      }
      memcpy(dst, src + 2, 1);
      memcpy(dst + 1, src + 1, 1);
      memcpy(dst + 2, src, 1);
      dst += 4;
      src += 4;
    }
  }
  scrub_tam_boundaries(tam, leny, lenx, cdimy, cdimx);
  if(plane_blit_sixel(s, &s->glyph, leny, lenx, 0, tam, SPRIXEL_INVALIDATED) < 0){
    goto error;
  }
  return 1;

error:
  if(!reuse){
    free(tam);
  }
  fbuf_free(&s->glyph);
  s->glyph.size = 0;
  return -1;
}

int fbcon_scrub(const struct ncpile* p, sprixel* s){
  return sixel_scrub(p, s);
}

#ifdef __linux__
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/ioctl.h>

int fbcon_rebuild(sprixel* s, int ycell, int xcell, uint8_t* auxvec){
  if(auxvec == NULL){
    return -1;
  }
  sprixcell_e state = SPRIXCELL_TRANSPARENT;
  for(int y = 0 ; y < s->cellpxy ; ++y){
    if(ycell * s->cellpxy + y >= s->pixy){
      break;
    }
    const size_t yoff = s->pixx * (ycell * s->cellpxy + y);
    for(int x = 0 ; x < s->cellpxx ; ++x){
      if(xcell * s->cellpxx + x >= s->pixx){
        break;
      }
      size_t offset = (yoff + xcell * s->cellpxx + x) * 4;
      const int vyx = (y % s->cellpxy) * s->cellpxx + x;
      if(x == 0 && y == 0){
        if(auxvec[vyx] == 0){
          state = SPRIXCELL_TRANSPARENT;
        }else{
          state = SPRIXCELL_OPAQUE_SIXEL;
        }
      }else{
        if(auxvec[vyx] == 0 && state == SPRIXCELL_OPAQUE_SIXEL){
          state = SPRIXCELL_MIXED_SIXEL;
        }else if(auxvec[vyx] && state == SPRIXCELL_TRANSPARENT){
          state = SPRIXCELL_MIXED_SIXEL;
        }
      }
      s->glyph.buf[offset + 3] = auxvec[vyx];
      offset += 4;
    }
  }
  s->n->tam[s->dimx * ycell + xcell].state = state;
  s->invalidated = SPRIXEL_INVALIDATED;
  return 1;
}

int fbcon_draw(const tinfo* ti, sprixel* s, int y, int x){
  int wrote = 0;
  for(unsigned l = 0 ; l < (unsigned)s->pixy && l + y * ti->cellpixy < ti->pixy ; ++l){
    // FIXME pixel size isn't necessarily 4B, line isn't necessarily psize*pixx
    size_t offset = ((l + y * ti->cellpixy) * ti->pixx + x * ti->cellpixx) * 4;
    uint8_t* tl = ti->linux_fbuffer + offset;
    const char* src = (char*)s->glyph.buf + (l * s->pixx * 4);
    for(unsigned c = 0 ; c < (unsigned)s->pixx && c < ti->pixx ; ++c){
      uint32_t pixel;
      memcpy(&pixel, src, 4);
      if(!rgba_trans_p(pixel, 0)){
        memcpy(tl, &pixel, 4);
        wrote += 4;
      }
      src += 4;
      tl += 4;
    }
  }
  return wrote;
}

void fbcon_scroll(const struct ncpile* p, tinfo* ti, int rows){
  if(ti->cellpixy < 1){
    return;
  }
  int totalrows = ti->cellpixy * p->dimy;
  int srows = rows * ti->cellpixy; // number of pixel rows being scrolled
  if(srows > totalrows){
    srows = totalrows;
  }
  int rowbytes = ti->cellpixx * p->dimx * 4;
  uint8_t* targ = ti->linux_fbuffer;
  uint8_t* src = ti->linux_fbuffer + srows * rowbytes;
  unsigned tocopy = rowbytes * (totalrows - srows);
  memmove(targ, src, tocopy);
  targ += tocopy;
  memset(targ, 0, srows * rowbytes);
}

// each row is a contiguous set of bits, starting at the msb
static inline size_t
row_bytes(const struct console_font_op* cfo){
  return (cfo->width + 7) / 8;
}

static inline size_t
glyph_bytes(const struct console_font_op* cfo){
  size_t minb = row_bytes(cfo) * cfo->height;
  return (minb + 31) / 32 * 32;
}

static unsigned char*
get_glyph(struct console_font_op* cfo, unsigned idx){
  if(idx >= cfo->charcount){
    return NULL;
  }
  return (unsigned char*)cfo->data + glyph_bytes(cfo) * idx;
}

// idx is the glyph index within cfo->data. qbits are the occupied quadrants:
//  0x8 = upper left
//  0x4 = upper right
//  0x2 = lower left
//  0x1 = lower right
static int
shim_quad_block(struct console_font_op* cfo, unsigned idx, unsigned qbits){
  unsigned char* glyph = get_glyph(cfo, idx);
  if(glyph == NULL){
    return -1;
  }
  unsigned r;
  for(r = 0 ; r < cfo->height / 2 ; ++r){
    unsigned char mask = 0x80;
    unsigned char* row = glyph + row_bytes(cfo) * r;
    unsigned x;
    *row = 0;
    for(x = 0 ; x < cfo->width / 2 ; ++x){
      if(qbits & 0x8){
        *row |= mask;
      }
      if((mask >>= 1) == 0){
        mask = 0x80;
        *++row = 0;
      }
    }
    while(x < cfo->width){
      if(qbits & 0x4){
        *row |= mask;
      }
      if((mask >>= 1) == 0){
        mask = 0x80;
        *++row = 0;
      }
      ++x;
    }
  }
  while(r < cfo->height){
    unsigned char mask = 0x80;
    unsigned char* row = glyph + row_bytes(cfo) * r;
    unsigned x;
    *row = 0;
    for(x = 0 ; x < cfo->width / 2 ; ++x){
      if(qbits & 0x2){
        *row |= mask;
      }
      if((mask >>= 1) == 0){
        mask = 0x80;
        *++row = 0;
      }
    }
    while(x < cfo->width){
      if(qbits & 0x1){
        *row |= mask;
      }
      if((mask >>= 1) == 0){
        mask = 0x80;
        *++row = 0;
      }
      ++x;
    }
    ++r;
  }
  return 0;
}

// use for drawing 1, 2, 3, 5, 6, and 7/8ths
static int
shim_lower_eighths(struct console_font_op* cfo, unsigned idx, int eighths){
  unsigned char* glyph = get_glyph(cfo, idx);
  if(glyph == NULL){
    return -1;
  }
  unsigned ten8ths = cfo->height * 10 / 8;
  unsigned start = cfo->height - (eighths * ten8ths / 10);
  unsigned r;
  for(r = 0 ; r < start ; ++r){
    unsigned char* row = glyph + row_bytes(cfo) * r;
    for(unsigned x = 0 ; x < cfo->width ; x += 8){
      row[x / 8] = 0;
    }
  }
  while(r < cfo->height){
    unsigned char* row = glyph + row_bytes(cfo) * r;
    for(unsigned x = 0 ; x < cfo->width ; x += 8){
      row[x / 8] = 0xff;
    }
    ++r;
  }
  return 0;
}

// add UCS2 codepoint |w| to |map| for font idx |fidx|
static int
add_to_map(struct unimapdesc* map, wchar_t w, unsigned fidx){
  logdebug("Adding mapping U+%04x -> %03u\n", w, fidx);
  struct unipair* tmp = realloc(map->entries, sizeof(*map->entries) * (map->entry_ct + 1));
  if(tmp == NULL){
    return -1;
  }
  map->entries = tmp;
  map->entries[map->entry_ct].unicode = w;
  map->entries[map->entry_ct].fontpos = fidx;
  ++map->entry_ct;
  return 0;
}

static int
program_line_drawing_chars(int fd, struct unimapdesc* map){
  struct simset {
    wchar_t* ws;
  } sets[] = {
    {
      .ws = L"/╱",
    }, {
      .ws = L"\\╲",
    }, {
      .ws = L"X╳☒",
    }, {
      .ws = L"O☐",
    }, {
      .ws = L"└┕┖┗╘╙╚╰",
    }, {
      .ws = L"┘┙┚┛╛╜╝╯",
    }, {
      .ws = L"┌┍┎┏╒╓╔╭",
    }, {
      .ws = L"┐┑┒┓╕╖╗╮",
    }, {
      .ws = L"─━┄┅┈┉╌╍═╼╾",
    }, {
      .ws = L"│┃┆┇┊┋╎╏║╽╿",
    }, {
      .ws = L"├┝┞┟┠┡┢┣╞╟╠",
    }, {
      .ws = L"┤┥┦┧┨┩┪┫╡╢╣",
    }, {
      .ws = L"┬┭┮┯┰┱┲┳╤╥╦",
    }, {
      .ws = L"┴┵┶┷┸┹┺┻╧╨╩",
    }, {
      .ws = L"┼┽┾┿╀╁╂╃╄╅╆╇╈╉╊╋╪╫╬",
    },
  };
  int toadd = 0;
  for(size_t sidx = 0 ; sidx < sizeof(sets) / sizeof(*sets) ; ++sidx){
    int fontidx = -1;
    struct simset* s = &sets[sidx];
    bool found[wcslen(s->ws)];
    memset(found, 0, sizeof(found));
    for(unsigned idx = 0 ; idx < map->entry_ct ; ++idx){
      for(size_t widx = 0 ; widx < wcslen(s->ws) ; ++widx){
        if(map->entries[idx].unicode == s->ws[widx]){
          logtrace("Found desired character U+%04x -> %03u\n",
                   map->entries[idx].unicode, map->entries[idx].fontpos);
          found[widx] = true;
          if(fontidx == -1){
            fontidx = map->entries[idx].fontpos;
          }
        }
      }
    }
    if(fontidx > -1){
      for(size_t widx = 0 ; widx < wcslen(s->ws) ; ++widx){
        if(!found[widx]){
          if(add_to_map(map, s->ws[widx], fontidx)){
            return -1;
          }
          ++toadd;
        }
      }
    }else{
      logwarn("Couldn't find any glyphs for set %zu\n", sidx);
    }
  }
  if(toadd == 0){
    return 0;
  }
  if(ioctl(fd, PIO_UNIMAP, map)){
    logwarn("Error setting kernel unicode map (%s)\n", strerror(errno));
    return -1;
  }
  loginfo("Successfully added %d kernel unicode mapping%s\n",
          toadd, toadd == 1 ? "" : "s");
  return 0;
}

// we have to keep a copy of the linux framebuffer while we reprogram fonts
struct framebuffer_copy {
  void* map;
  size_t maplen;
  unsigned pixely, pixelx;
};

// build |fbdup| from the framebuffer owned by ti, which will be closed. this
// is a necessary step prior to reprogramming the console font.
static int
copy_and_close_linux_fb(tinfo* ti, struct framebuffer_copy* fbdup){
  if((fbdup->map = memdup(ti->linux_fbuffer, ti->linux_fb_len)) == NULL){
    return -1;
  }
  munmap(ti->linux_fbuffer, ti->linux_fb_len);
  fbdup->maplen = ti->linux_fb_len;
  ti->linux_fbuffer = NULL;
  ti->linux_fb_len = 0;
  fbdup->pixely = ti->pixy;
  fbdup->pixelx = ti->pixx;
  return 0;
}

static void
kill_fbcopy(struct framebuffer_copy* fbdup){
  free(fbdup->map);
}

static int
program_block_drawing_chars(tinfo* ti, int fd, struct console_font_op* cfo,
                            struct unimapdesc* map, unsigned no_font_changes,
                            bool* halfblocks, bool* quadrants){
  struct shimmer {
    unsigned qbits;
    wchar_t w;
    bool found;
  };
  struct shimmer half[] = {
    { .qbits = 0xc, .w = L'▀', .found = false, },
    { .qbits = 0x3, .w = L'▄', .found = false, },
  };
  struct shimmer quads[] = {
    // if we get these first two, we have the halfblocks
    { .qbits = 0xa, .w = L'▌', .found = false, },
    { .qbits = 0x5, .w = L'▐', .found = false, },
    { .qbits = 0x8, .w = L'▘', .found = false, },
    { .qbits = 0x4, .w = L'▝', .found = false, },
    { .qbits = 0x2, .w = L'▖', .found = false, },
    { .qbits = 0x1, .w = L'▗', .found = false, },
    { .qbits = 0x7, .w = L'▟', .found = false, },
    { .qbits = 0xb, .w = L'▙', .found = false, },
    { .qbits = 0xd, .w = L'▜', .found = false, },
    { .qbits = 0xe, .w = L'▛', .found = false, },
    { .qbits = 0x9, .w = L'▚', .found = false, },
    { .qbits = 0x6, .w = L'▞', .found = false, },
  };
  struct shimmer eighths[] = {
    { .qbits = 7, .w = L'▇', .found = false, },
    { .qbits = 6, .w = L'▆', .found = false, },
    { .qbits = 5, .w = L'▅', .found = false, },
    { .qbits = 3, .w = L'▃', .found = false, },
    { .qbits = 2, .w = L'▂', .found = false, },
    { .qbits = 1, .w = L'▁', .found = false, },
  };
  // first, take a pass to see which glyphs we already have
  size_t numfound = 0;
  size_t halvesfound = 0;
  for(unsigned i = 0 ; i < cfo->charcount ; ++i){
    if(map->entries[i].unicode >= 0x2580 && map->entries[i].unicode <= 0x259f){
      for(size_t s = 0 ; s < sizeof(half) / sizeof(*half) ; ++s){
        if(map->entries[i].unicode == half[s].w){
          logdebug("Found %lc at fontidx %u\n", half[s].w, i);
          half[s].found = true;
          ++halvesfound;
          break;
        }
      }
      for(size_t s = 0 ; s < sizeof(quads) / sizeof(*quads) ; ++s){
        if(map->entries[i].unicode == quads[s].w){
          logdebug("Found %lc at fontidx %u\n", quads[s].w, i);
          quads[s].found = true;
          ++numfound;
          break;
        }
      }
      for(size_t s = 0 ; s < sizeof(eighths) / sizeof(*eighths) ; ++s){
        if(map->entries[i].unicode == eighths[s].w){
          logdebug("Found %lc at fontidx %u\n", eighths[s].w, i);
          eighths[s].found = true;
          ++numfound;
          break;
        }
      }
    }
  }
  if(halvesfound == sizeof(half) / sizeof(*half)){
    *halfblocks = true;
  }
  if(numfound + halvesfound == (sizeof(half) + sizeof(quads) + sizeof(eighths)) / sizeof(*quads)){
    logdebug("all %zu desired glyphs were already present\n", numfound);
    *quadrants = true;
    return 0;
  }
  if(no_font_changes){
    logdebug("not reprogramming kernel font per request\n");
    return 0;
  }
  int added = 0;
  int halvesadded = 0;
  unsigned candidate = cfo->charcount;
  // FIXME factor out a function here, crikey
  for(size_t s = 0 ; s < sizeof(half) / sizeof(*half) ; ++s){
    if(!half[s].found){
      while(--candidate){
        if(map->entries[candidate].unicode < 0x2580 || map->entries[candidate].unicode > 0x259f){
          break;
        }
      }
      if(candidate == 0){
        logwarn("Ran out of replaceable glyphs for U+%04lx\n", (long)half[s].w);
        // FIXME maybe don't want to error out here?
        return -1;
      }
      if(shim_quad_block(cfo, candidate, half[s].qbits)){
        logwarn("Error replacing glyph for U+%04lx at %u\n", (long)half[s].w, candidate);
        return -1;
      }
      if(add_to_map(map, half[s].w, candidate)){
        return -1;
      }
      ++halvesadded;
    }
  }
  for(size_t s = 0 ; s < sizeof(quads) / sizeof(*quads) ; ++s){
    if(!quads[s].found){
      while(--candidate){
        if(map->entries[candidate].unicode < 0x2580 || map->entries[candidate].unicode > 0x259f){
          break;
        }
      }
      if(candidate == 0){
        logwarn("Ran out of replaceable glyphs for U+%04lx\n", (long)quads[s].w);
        // FIXME maybe don't want to error out here?
        return -1;
      }
      if(shim_quad_block(cfo, candidate, quads[s].qbits)){
        logwarn("Error replacing glyph for U+%04lx at %u\n", (long)quads[s].w, candidate);
        return -1;
      }
      if(add_to_map(map, quads[s].w, candidate)){
        return -1;
      }
      ++added;
    }
  }
  for(size_t s = 0 ; s < sizeof(eighths) / sizeof(*eighths) ; ++s){
    if(!eighths[s].found){
      while(--candidate){
        if(map->entries[candidate].unicode < 0x2580 || map->entries[candidate].unicode > 0x259f){
          break;
        }
      }
      if(candidate == 0){
        logwarn("Ran out of replaceable glyphs for U+%04lx\n", (long)eighths[s].w);
        return -1;
      }
      if(shim_lower_eighths(cfo, candidate, eighths[s].qbits)){
        logwarn("Error replacing glyph for U+%04lx at %u\n", (long)eighths[s].w, candidate);
        return -1;
      }
      if(add_to_map(map, eighths[s].w, candidate)){
        return -1;
      }
      ++added;
    }
  }
  if(halvesadded == 0 && added == 0){
    loginfo("didn't replace any glyphs, not calling ioctl\n");
    return 0;
  }
  struct framebuffer_copy fbdup;
  if(copy_and_close_linux_fb(ti, &fbdup)){
    return -1;
  }
  cfo->op = KD_FONT_OP_SET;
  if(ioctl(fd, KDFONTOP, cfo)){
    logwarn("Error programming kernel font (%s)\n", strerror(errno));
    kill_fbcopy(&fbdup);
    return -1;
  }
  if(ioctl(fd, PIO_UNIMAP, map)){
    logwarn("Error setting kernel unicode map (%s)\n", strerror(errno));
    kill_fbcopy(&fbdup);
    return -1;
  }
  if(halvesadded + halvesfound == sizeof(half) / sizeof(*half)){
    *halfblocks = true;
  }
  if(added + numfound == (sizeof(quads) + sizeof(eighths)) / sizeof(*quads)){
    *quadrants = true;
  }
  added += halvesadded;
  loginfo("successfully added %d kernel font glyph%s\n", added, added == 1 ? "" : "s");
  unsigned pixely, pixelx;
  if(get_linux_fb_pixelgeom(ti, &pixely, &pixelx)){
    kill_fbcopy(&fbdup);
    return -1;
  }
  if(pixely != fbdup.pixely || pixelx != fbdup.pixelx || ti->linux_fb_len != fbdup.maplen){
    logwarn("framebuffer changed size, not reblitting\n");
  }else{
    memcpy(ti->linux_fbuffer, fbdup.map, fbdup.maplen);
  }
  kill_fbcopy(&fbdup);
  return 0;
}

static int
reprogram_linux_font(tinfo* ti, int fd, struct console_font_op* cfo,
                     struct unimapdesc* map, unsigned no_font_changes,
                     bool* halfblocks, bool* quadrants){
  if(ioctl(fd, KDFONTOP, cfo)){
    logwarn("Error reading Linux kernelfont (%s)\n", strerror(errno));
    return -1;
  }
  loginfo("Kernel font size (glyphcount): %hu\n", cfo->charcount);
  loginfo("Kernel font character geometry: %hux%hu\n", cfo->width, cfo->height);
  if(cfo->charcount > 512){
    logwarn("Warning: kernel returned excess charcount\n");
    return -1;
  }
  if(ioctl(fd, GIO_UNIMAP, map)){
    logwarn("Error reading Linux unimap (%s)\n", strerror(errno));
    return -1;
  }
  loginfo("Kernel Unimap size: %hu/%hu\n", map->entry_ct, USHRT_MAX);
  // for certain sets of characters, we're not going to draw them in, but we
  // do want to ensure they map to something plausible...this doesn't reset
  // the framebuffer, even if we do some reprogramming.
  if(!no_font_changes){
    if(program_line_drawing_chars(fd, map)){
      return -1;
    }
  }
  if(program_block_drawing_chars(ti, fd, cfo, map, no_font_changes,
                                 halfblocks, quadrants)){
    return -1;
  }
  return 0;
}

int reprogram_console_font(tinfo* ti, unsigned no_font_changes,
                           bool* halfblocks, bool* quadrants){
  struct console_font_op cfo = {
    .op = KD_FONT_OP_GET,
    .charcount = 512,
    .height = 32,
    .width = 32,
  };
  size_t totsize = 128 * cfo.charcount; // FIXME enough?
  cfo.data = malloc(totsize);
  if(cfo.data == NULL){
    logwarn("Error acquiring %zub for font descriptors (%s)\n", totsize, strerror(errno));
    return -1;
  }
  struct unimapdesc map = {};
  map.entry_ct = USHRT_MAX;
  totsize = map.entry_ct * sizeof(struct unipair);
  map.entries = malloc(totsize);
  if(map.entries == NULL){
    logwarn("Error acquiring %zub for Unicode font map (%s)\n", totsize, strerror(errno));
    free(cfo.data);
    return -1;
  }
  int r = reprogram_linux_font(ti, ti->ttyfd, &cfo, &map, no_font_changes,
                               halfblocks, quadrants);
  free(cfo.data);
  free(map.entries);
  return r;
}

// is the provided fd a Linux console? if so, returns true. if it is indeed
// a Linux console, and the console font has the quadrant glyphs (either
// because they were already present, or we added them), quadrants is set high.
bool is_linux_console(int fd){
  if(fd < 0){
    return false;
  }
  int mode;
  if(ioctl(fd, KDGETMODE, &mode)){
    logdebug("Not a Linux console, KDGETMODE failed\n");
    return false;
  }
  loginfo("Verified Linux console, mode %d\n", mode);
  return true;
}

int get_linux_fb_pixelgeom(tinfo* ti, unsigned* ypix, unsigned *xpix){
  unsigned fakey, fakex;
  if(ypix == NULL){
    ypix = &fakey;
  }
  if(xpix == NULL){
    xpix = &fakex;
  }
  struct fb_var_screeninfo fbi = {};
  if(ioctl(ti->linux_fb_fd, FBIOGET_VSCREENINFO, &fbi)){
    logwarn("No framebuffer info from %s (%s?)\n", ti->linux_fb_dev, strerror(errno));
    return -1;
  }
  loginfo("Linux %s geometry: %dx%d\n", ti->linux_fb_dev, fbi.yres, fbi.xres);
  *ypix = fbi.yres;
  *xpix = fbi.xres;
  size_t len = *ypix * *xpix * fbi.bits_per_pixel / 8;
  if(ti->linux_fb_len != len){
    if(ti->linux_fbuffer != MAP_FAILED){
      munmap(ti->linux_fbuffer, ti->linux_fb_len);
      ti->linux_fbuffer = MAP_FAILED;
      ti->linux_fb_len = 0;
    }
    ti->linux_fbuffer = mmap(NULL, len, PROT_READ|PROT_WRITE,
                             MAP_SHARED, ti->linux_fb_fd, 0);
    if(ti->linux_fbuffer == MAP_FAILED){
      logerror("Couldn't map %zuB on %s (%s?)\n", len, ti->linux_fb_dev, strerror(errno));
      return -1;
    }
    ti->linux_fb_len = len;
    loginfo("Mapped %zuB on %s\n", len, ti->linux_fb_dev);
  }
  return 0;
}

bool is_linux_framebuffer(tinfo* ti){
  // FIXME there might be multiple framebuffers present; how do we determine
  // which one is ours?
  const char* dev = "/dev/fb0";
  loginfo("Checking for Linux framebuffer at %s\n", dev);
  int fd = open(dev, O_RDWR | O_CLOEXEC);
  if(fd < 0){
    logdebug("Couldn't open framebuffer device %s\n", dev);
    return false;
  }
  ti->linux_fb_fd = fd;
  if((ti->linux_fb_dev = strdup(dev)) == NULL){
    close(ti->linux_fb_fd);
    ti->linux_fb_fd = -1;
    return false;
  }
  if(get_linux_fb_pixelgeom(ti, &ti->pixy, &ti->pixx)){
    close(fd);
    ti->linux_fb_fd = -1;
    free(ti->linux_fb_dev);
    ti->linux_fb_dev = NULL;
    return false;
  }
  return true;
}
#else
int fbcon_rebuild(sprixel* s, int ycell, int xcell, uint8_t* auxvec){
  (void)s;
  (void)ycell;
  (void)xcell;
  (void)auxvec;
  return 0;
}

int fbcon_draw(const tinfo* ti, sprixel* s, int y, int x){
  (void)ti;
  (void)s;
  (void)y;
  (void)x;
  return 0;
}

void fbcon_scroll(const struct ncpile* p, tinfo* ti, int rows){
  (void)p;
  (void)ti;
  (void)rows;
}

int get_linux_fb_pixelgeom(tinfo* ti, unsigned* ypix, unsigned *xpix){
  (void)ti;
  (void)ypix;
  (void)xpix;
  return -1;
}
#endif
