#include "view.h"

unsigned row, col;
int p = 0;

int dirty = 0;
char status[STATSZ], error[ERRSZ];
char text[N_TEXTSZ];
unsigned textp = 0;

char hist[HISTSZ][N_TEXTSZ];
unsigned hista[HISTSZ], histh[HISTSZ];
time_t histt[HISTSZ];
unsigned histn = 0, histi = 0, histip, histscroll;

unsigned menu = M_MAIN;
