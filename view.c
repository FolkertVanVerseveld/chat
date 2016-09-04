#include "view.h"
#include "text.h"
#include "smt.h"
#include <ncurses.h>

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

struct ls ls;
unsigned io_filei = 0, io_select = 0;

pthread_mutex_t gevlock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  gevpush = PTHREAD_COND_INITIALIZER;

void setcol(int col)
{
	if (p != col) {
		attroff(COLOR_PAIR(p));
		attron(COLOR_PAIR(col));
		p = col;
	}
}

void drawhdr(void)
{
	if (cfg.mode & MODE_GUI)
		smthdr();
	else
		txthdr();
}

void drawmain(void)
{
	if (dirty & EV_TEXT) {
		histcalc();
		unsigned i, j, y;
		for (i = histip + histscroll, y = 1; i < HISTSZ; ++i) {
			j = (histi + histn + i) % HISTSZ;
			char hdr[COL_TXT];
			hdr[0] = '\0';
			if (hist[j][0]) {
				struct tm t;
				localtime_r(&histt[j], &t);
				unsigned i = strftime(hdr, sizeof hdr, "%F %H:%M:%S", &t);
				if (!(hista[j] & HA_OTHER)) {
					while (i < COL_TXT - 3)
						hdr[i++] = ' ';
					hdr[COL_TXT - 3] = 'm';
					hdr[COL_TXT - 2] = 'e';
				}
			}
			hdr[COL_TXT - 1] = '\0';
			mvaddstr(y, 0, hdr);
			clrtoeol();
			wrapaddstr(y, COL_TXT, COL_TXT, hist[j]);
			y += histh[j];
			clrtoeol();
		}
		for (; y <= B_TXT; ++y) {
			move(y, 0);
			clrtoeol();
		}
	}
}

void drawsend(void)
{
	unsigned i, y;
	int c_def = p;
	if (io_select < io_filei)
		io_filei = io_select;
	if (io_select > B_TXT)
		io_filei = io_select - B_TXT + 1;
	for (i = io_filei, y = 1; i < ls.n && y <= B_TXT; ++y, ++i) {
		if (i == io_select)
			color(COL_BLACK, COL_WHITE);
		else
			setcol(c_def);
		wrapaddstr(y, 1, 1, ls.list[i]->d_name);
		clrtoeol();
	}
	setcol(c_def);
	for (; y <= B_TXT; ++y) {
		move(y, 0);
		clrtoeol();
	}
}

int viewmain(void)
{
	if (cfg.mode & MODE_GUI)
		return smtmain();
	else
		return txtmain();
}
