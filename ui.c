#include "ui.h"
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <ncurses.h>

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

#define EV_STATUS 1

static int dirty = 0;
static char status[256];

void uiperror(const char *str)
{
	pthread_mutex_lock(&gevlock);
	snprintf(status, 256, "%s: %s\n", str, strerror(errno));
	dirty |= EV_STATUS;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uistatus(const char *str)
{
	pthread_mutex_lock(&gevlock);
	strncpy(status, str, 256);
	status[255] = '\0';
	dirty |= EV_STATUS;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uistatusf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	pthread_mutex_lock(&gevlock);
	vsnprintf(status, 256, format, args);
	dirty |= EV_STATUS;
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
	va_end(args);
}

static void color(unsigned fg, unsigned bg)
{
	int col = bg * COL_COUNT + COL_COUNT - fg - 1;
	if (col < 0) col = 0;
	if (col >= COL_MAX)
		col = COL_MAX;
	if (p != col) {
		attroff(COLOR_PAIR(p));
		attron(COLOR_PAIR(col));
		p = col;
	}
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
	while (running) {
		if (gettimeofday(&time, NULL) != 0)
			goto unlock;
		delta.tv_sec = time.tv_sec;
		delta.tv_nsec = time.tv_usec * 1000LU + 200 * 1000000LU;
		ret = pthread_cond_timedwait(&gevpush, &gevlock, &delta);
		if (dirty & EV_STATUS) {
			mvaddstr(0, 0, status);
			clrtoeol();
			dirty &= ~EV_STATUS;
		}
		mvaddch(1, 0, yay[i]);
		i ^= 1;
		clrtoeol();
		refresh();
		while ((key = getch()) != ERR) {
			if (key == KEY_RESIZE)
				reshape();
			if (key == 'q') {
				running = 0;
				break;
			}
		}
	}
unlock:
	if (pthread_mutex_unlock(&gevlock) != 0)
		abort();
	uifree();
	return 0;
}
