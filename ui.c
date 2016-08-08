#include "ui.h"
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <ncurses.h>
#include "net.h"

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

static WINDOW *scr = NULL;
static pthread_mutex_t gevlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gevpush = PTHREAD_COND_INITIALIZER;
static int row, col, p = 0;

#define HA_OTHER 1

#define EV_STATUS 1
#define EV_ERROR 2
#define EV_TEXT 4

#define STATSZ 256
#define ERRSZ 256

#define HISTSZ 8

static int dirty = 0;
static char status[STATSZ], error[ERRSZ];
static char text[N_TEXTSZ];
static unsigned textp = 0;

static char hist[HISTSZ][N_TEXTSZ];
static unsigned hista[HISTSZ];
static unsigned histn = 0, histi = 0;
// current position is computed as (histi + histn) % HISTSZ

static void histadd(const char *str, unsigned attr)
{
	unsigned i = (histi + histn) % HISTSZ;
	strncpy(hist[i], str, N_TEXTSZ);
	hist[i][N_TEXTSZ - 1] = '\0';
	hista[i] = attr;
	if (histn < HISTSZ)
		++histn;
	else
		histi = (histi + 1) % HISTSZ;
	dirty |= EV_TEXT;
}

void uitext(const char *str)
{
	pthread_mutex_lock(&gevlock);
	histadd(str, HA_OTHER);
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uiperror(const char *str)
{
	pthread_mutex_lock(&gevlock);
	snprintf(error, ERRSZ, "%s: %s\n", str, strerror(errno));
	dirty |= EV_ERROR;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uistatus(const char *str)
{
	pthread_mutex_lock(&gevlock);
	strncpy(status, str, STATSZ);
	status[STATSZ - 1] = '\0';
	dirty |= EV_STATUS;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uistatusf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	pthread_mutex_lock(&gevlock);
	vsnprintf(status, STATSZ, format, args);
	dirty |= EV_STATUS;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
	va_end(args);
}

void uierror(const char *err)
{
	pthread_mutex_lock(&gevlock);
	strncpy(error, err, ERRSZ);
	error[ERRSZ - 1] = '\0';
	dirty |= EV_ERROR;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uierrorf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	pthread_mutex_lock(&gevlock);
	vsnprintf(error, ERRSZ, format, args);
	dirty |= EV_ERROR;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
	va_end(args);
}

static void setcol(int col)
{
	if (p != col) {
		attroff(COLOR_PAIR(p));
		attron(COLOR_PAIR(col));
		p = col;
	}
}

static void color(unsigned fg, unsigned bg)
{
	int col = bg * COL_COUNT + COL_COUNT - fg - 1;
	if (col < 0) col = 0;
	if (col >= COL_MAX)
		col = COL_MAX;
	setcol(col);
}

static void reshape(void)
{
	int y, x;
	getmaxyx(scr, y, x);
	if (y < ROW_MIN || x < COL_MIN) {
		clear();
		mvaddstr(0, 0, "tty too small");
		refresh();
		napms(1500);
	}
	row = y;
	col = x;
}

static void kbp(int key)
{
	unsigned t_dirty = 0;
	int ch = key & 0xff;
	if (key == 263 || ch == '\b') {
		if (textp) {
			text[--textp] = '\0';
			t_dirty = 1;
		}
	} else if (ch == '\n' || ch == '\r') {
		int ns = nettext(text);
		histadd(text, 0);
		text[textp = 0] = '\0';
		t_dirty = 1;
	} else if (isprint(ch)) {
		if (textp < N_TEXTSZ - 1) {
			text[textp++] = ch;
			text[textp] = '\0';
			t_dirty = 1;
		}
	}
	if (t_dirty) {
		mvaddstr(row - 2, 0, text);
		clrtoeol();
		refresh();
	}
}

static void uifree(void)
{
	if (scr) {
		delwin(scr);
		scr = NULL;
	}
	endwin();
}

static int uiinit(void)
{
	const char *err = "unknown";
	if (scr) return 1;
	scr = initscr();
	if (!scr) {
		err = "no screen available";
		goto fail;
	}
	reshape();
	if (has_colors() == FALSE) {
		err = "no colors";
		goto fail;
	}
	start_color();
	cbreak();
	keypad(scr, TRUE);
	noecho();
	nonl();
	halfdelay(4);
	clear();
	unsigned fc, bc, p, i;
	for (bc = 0, i = p = 1, fc = COL_COUNT - 2; i < COL_COUNT; ++i, --fc, ++p)
		if (init_pair(p, fc, bc) == ERR) {
			err = "init_pair failed";
			goto fail;
		}
	for (++bc; bc < COL_COUNT; ++bc)
		for (i = 0, fc = COL_COUNT - 1; i < COL_COUNT; ++i, --fc, ++p)
			if (init_pair(p, fc, bc) == ERR) {
				err = "init_pair failed";
				goto fail;
			}
	for (i = 0; i < HISTSZ; ++i) {
		hist[i][0] = '\0';
		hista[i] = 0;
	}
	return 0;
fail:
	uifree();
	fprintf(stderr, "%s\n", err);
	return 1;
}

int uimain(void)
{
	struct timeval time;
	struct timespec delta;
	int key, ret, running = 0;
	const char *yay = "+-";
	int i = 0;
	if (uiinit())
		return 1;
	running = 1;
	if (pthread_mutex_lock(&gevlock) != 0)
		abort();
	mvaddstr(0, 0, "F2:quit F3:send");
	refresh();
	while (running) {
		if (gettimeofday(&time, NULL) != 0)
			goto unlock;
		delta.tv_sec = time.tv_sec;
		delta.tv_nsec = time.tv_usec + 50 * 1000000LU;
		ret = pthread_cond_timedwait(&gevpush, &gevlock, &delta);
		if (dirty & EV_ERROR) {
			int col = p;
			color(COL_RED, COL_BLACK);
			mvaddstr(row - 1, 0, error);
			clrtoeol();
			setcol(col);
			dirty &= ~EV_ERROR;
		} else if (dirty & EV_STATUS) {
			mvaddstr(row - 1, 0, status);
			clrtoeol();
			dirty &= ~EV_STATUS;
		}
		if (dirty & EV_TEXT) {
			for (unsigned j, i = 0; i < HISTSZ; ++i) {
				j = (histi + histn + i) % HISTSZ;
				const char *hdr = "  ";
				if (hist[j][0] && !(hista[j] & HA_OTHER))
					hdr = "me";
				mvaddstr(i + 1, 0, hdr);
				mvaddstr(i + 1, 3, hist[j]);
				clrtoeol();
			}
		}
		mvaddch(row - 1, col - 2, yay[i]);
		i ^= 1;
		clrtoeol();
		refresh();
		while ((key = getch()) != ERR) {
			if (key == KEY_RESIZE)
				reshape();
			if (key == KEY_F(2)) {
				running = 0;
				break;
			} else
				kbp(key);
		}
	}
unlock:
	if (pthread_mutex_unlock(&gevlock) != 0)
		abort();
	uifree();
	return 0;
}
