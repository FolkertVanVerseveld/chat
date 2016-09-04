#ifndef VIEW_H
#define VIEW_H

#include "config.h"
#include "fs.h"
#include "net.h"
#include <time.h>
#include <pthread.h>

/* internal user interface components
that do not get exposed through ui.h */

#define COL_BLACK 0
#define COL_RED 1
#define COL_GREEN 2
#define COL_YELLOW 3
#define COL_BLUE 4
#define COL_MAGENTA 5
#define COL_CYAN 6
#define COL_WHITE 7
#define COL_COUNT 8
#define COL_MAX (COL_COUNT*COL_COUNT-1)

#define ROW_MIN 12
#define COL_MIN 60

extern unsigned row, col;
extern int p;

#define HA_OTHER 1

#define EV_STATUS 1
#define EV_ERROR 2
#define EV_TEXT 4

#define STATSZ 256
#define ERRSZ 256

#define HISTSZ 256

extern int dirty;
extern char status[STATSZ], error[ERRSZ];
extern char text[N_TEXTSZ];
extern unsigned textp;

#define COL_TXT 24

#define B_ROW(y) (row-(y))
#define B_TXT B_ROW(3)

extern char hist[HISTSZ][N_TEXTSZ];
extern unsigned hista[HISTSZ], histh[HISTSZ];
extern time_t histt[HISTSZ];
extern unsigned histn, histi, histip, histscroll;
// current position is computed as (histi + histn) % HISTSZ

#define M_MAIN 0
#define M_FILE 1

extern unsigned menu;

extern struct ls ls;
extern unsigned io_filei, io_select;

extern pthread_mutex_t gevlock;
extern pthread_cond_t  gevpush;

void kbp(int key);
void histcalc(void);
void setcol(int col);
void color(unsigned fg, unsigned bg);
void reshape(void);
void drawhdr(void);
void wrapaddstr(unsigned y, unsigned x, unsigned d, char *str);

int viewmain(void);
void drawmain(void);
void drawsend(void);
void goto_menu(unsigned m);

#endif
