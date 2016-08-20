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
#include "fs.h"
#include "string.h"
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
static int p = 0;
static unsigned row, col;

#define HA_OTHER 1

#define EV_STATUS 1
#define EV_ERROR 2
#define EV_TEXT 4

#define STATSZ 256
#define ERRSZ 256

#define HISTSZ 256

static int dirty = 0;
static char status[STATSZ], error[ERRSZ];
static char text[N_TEXTSZ];
static unsigned textp = 0;

#define COL_TXT 3

#define B_ROW(y) (row-(y))
#define B_TXT B_ROW(3)

static char hist[HISTSZ][N_TEXTSZ];
static unsigned hista[HISTSZ], histh[HISTSZ];
static unsigned histn = 0, histi = 0, histip, histscroll;
// current position is computed as (histi + histn) % HISTSZ

#define M_MAIN 0
#define M_FILE 1

static unsigned menu = M_MAIN;

static void goto_menu(unsigned m);

struct ls ls;
static unsigned io_filei = 0, io_select = 0;

// default behavior causes deadlock when run in UI thread
#undef nschk
#define nschk(x) \
	if (x != NS_OK) {\
		switch (x) {\
		case NS_LEFT:strerr("other left");goto fail;\
		default:errorf("network error: code %u",x);goto fail;\
		}\
	}

static void histcalc(void)
{
	unsigned sum = 0, i, h, c;
	for (i = 0; i < HISTSZ; ++i) {
		h = 1; c = col - COL_TXT;
		for (const char *str = hist[i]; *str; ++str, --c)
			if (!c || *str == '\n') {
				c = col - COL_TXT;
				++h;
			}
		sum += histh[i] = h;
	}
	histip = sum > B_TXT ? sum - B_TXT : 0;
}

static void histadd(const char *str, unsigned attr)
{
	unsigned i = (histi + histn) % HISTSZ;
	strncpyz(hist[i], str, N_TEXTSZ);
	hista[i] = attr;
	if (histn < HISTSZ)
		++histn;
	else
		histi = (histi + 1) % HISTSZ;
	dirty |= EV_TEXT;
}

/* internal fast ui status update routines */
static inline void strperror(const char *str)
{
	snprintf(error, ERRSZ, "%s: %s\n", str, strerror(errno));
	dirty |= EV_ERROR;
}

static inline void strstatus(const char *str)
{
	strncpyz(status, str, STATSZ);
	dirty |= EV_STATUS;
}

static inline void vstatusf(const char *format, va_list args)
{
	vsnprintf(status, STATSZ, format, args);
	dirty |= EV_STATUS;
}

static inline void statusf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vstatusf(format, args);
	va_end(args);
}

static inline void strerr(const char *err)
{
	strncpyz(error, err, ERRSZ);
	dirty |= EV_ERROR;
}

static inline void verrorf(const char *format, va_list args)
{
	vsnprintf(error, ERRSZ, format, args);
	dirty |= EV_ERROR;
}

static inline void errorf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	verrorf(format, args);
	va_end(args);
}

static void wrapaddstr(unsigned y, unsigned x, unsigned d, char *str)
{ if (d >= col) {
		sprintf(error, "wrap error: d > col: d=%u,col=%u", d, col);
		dirty |= EV_ERROR;
		return;
	}
	unsigned c = col - d;
	size_t len = strlen(str);
	// don't bother doing complicated things if
	// we can do it the easy and simple way
	if (len < c) {
		mvaddstr(y, x, str);
		clrtoeol();
		return;
	}
	char *old = str;
	for (; *str; ++str, --c)
		if (!c || *str == '\n') {
			c = col - d;
			int tmp = *str;
			*str = '\0';
			mvaddstr(y, x, old);
			old = str;
			*str = tmp;
			++y;
		}
	if (*old)
		mvaddstr(y, x, old);
}

/*
thread-safe ui status updates
the ui thread is always notified because
status updates are high-priority events
*/
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
	strperror(str);
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uistatus(const char *str)
{
	pthread_mutex_lock(&gevlock);
	strstatus(str);
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uistatusf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	pthread_mutex_lock(&gevlock);
	vstatusf(format, args);
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
	va_end(args);
}

void uierror(const char *err)
{
	pthread_mutex_lock(&gevlock);
	strerr(err);
	pthread_cond_signal(&gevpush);
	pthread_mutex_unlock(&gevlock);
}

void uierrorf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	pthread_mutex_lock(&gevlock);
	verrorf(format, args);
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

static void uihdr(void)
{
	mvaddstr(0, 0, menu == M_MAIN ? "F2:quit F3:send" : "F2:quit F3:cancel");
	refresh();
}

static void reshape(void)
{
	int y, x;
	getmaxyx(scr, y, x);
	clear();
	if (y < ROW_MIN || x < COL_MIN) {
		mvaddstr(0, 0, "tty too small");
		refresh();
		napms(1500);
	}
	row = y;
	col = x;
	dirty |= EV_STATUS | EV_TEXT;
	// XXX not elegant, but it works
	histscroll = io_filei = 0;
	uihdr();
	if (menu == M_MAIN)
		histcalc();
	move(row - 2, textp);
}

static void filefilter(void)
{
	io_select = ls.n;
	for (unsigned i = 0; i < ls.n; ++i)
		if (!strncmp(text, ls.list[i]->d_name, textp)) {
			io_select = i;
			return;
		}
}

static int kbp_send()
{
	if (net_fd == -1) {
		strerr("client not connected");
		return 0;
	}
	int sp = 0;
	// don't send only whitespace
	for (unsigned i = 0; i < textp; ++i)
		if (!isspace(text[i])) {
			sp = 1;
			break;
		}
	if (!sp) return 0;
	int ns = nettext(text);
	nschk(ns);
	histadd(text, 0);
	text[textp = 0] = '\0';
	return 1;
fail:
	return 0;
}

static int kbp_select()
{
	if (io_select >= ls.n)
		strstatus("send aborted");
	else if (!strcmp(ls.list[io_select]->d_name, ".")) {
		struct dirent *e = ls.list[io_select];
		if (d_isdir(e) && !ls_cd(&ls, e->d_name)) {
			text[textp = 0] = '\0';
			return 1;
		}
		strstatus("send aborted");
	} else {
		struct dirent *e = ls.list[io_select];
		if (d_isdir(e)) {
			if (!ls_cd(&ls, e->d_name)) {
				text[textp = 0] = '\0';
				return 1;
			}
			strerr("internal error");
			return 0;
		}
		if (sq_put(ls.path, e->d_name))
			strerr("send failed");
		else
			statusf("sending \"%s\"", e->d_name);
	}
	goto_menu(M_MAIN);
	return 1;
}

static void kbp(int key)
{
	unsigned t_dirty = 0;
	int ch = key & 0xff;
	unsigned b_len = menu == M_MAIN ? histn : ls.n;
	if (B_TXT > b_len) b_len = B_TXT;
	if (b_len > 0) {
		if (key == 258) {
			if (menu == M_MAIN)
				histscroll = (histscroll + 1) % b_len;
			else if (ls.n)
				io_filei = (io_filei + 1) % b_len;
		} else if (key == 259) {
			if (menu == M_MAIN)
				histscroll = (histscroll + b_len - 1) % b_len;
			else if (ls.n)
				io_filei = (io_filei + b_len - 1) % b_len;
		}
	}
	if (key == 263 || ch == '\b') {
		if (textp) {
			text[--textp] = '\0';
			t_dirty = 1;
		}
	} else if (ch == '\n' || ch == '\r')
		t_dirty = menu == M_MAIN ? kbp_send() : kbp_select();
	else if (isprint(ch)) {
		if (textp < N_TEXTSZ - 1) {
			text[textp++] = ch;
			text[textp] = '\0';
			t_dirty = 1;
		}
	}
	if (t_dirty) {
		if (menu == M_FILE)
			filefilter();
		mvaddstr(row - 2, 0, text);
		clrtoeol();
		refresh();
	}
}

static void uifree(void)
{
	ls_free(&ls);
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
	halfdelay(1);
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

static void drawmain(void)
{
	if (dirty & EV_TEXT) {
		histcalc();
		unsigned i, j, y;
		for (i = histip + histscroll, y = 1; i < HISTSZ; ++i) {
			j = (histi + histn + i) % HISTSZ;
			const char *hdr = "  ";
			if (hist[j][0] && !(hista[j] & HA_OTHER))
				hdr = "me";
			mvaddstr(y, 0, hdr);
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

static void drawsend(void)
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


int uimain(void)
{
	struct timeval time;
	struct timespec delta;
	int key, running = 0;
	const char *yay = "+-";
	int i = 0;
	if (uiinit())
		return 1;
	running = 1;
	if (pthread_mutex_lock(&gevlock) != 0)
		abort();
	uihdr();
	struct net_state state;
	state.transfers = 1;
	state.tries = 0;
	state.send[0] = state.recv[0] = '\0';
	while (running) {
		if (gettimeofday(&time, NULL) != 0)
			goto unlock;
		delta.tv_sec = time.tv_sec;
		delta.tv_nsec = time.tv_usec * 1000LU + 100 * 1000000LU;
		pthread_cond_timedwait(&gevpush, &gevlock, &delta);
		curs_set(0);
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
		if (menu == M_MAIN)
			drawmain();
		else
			drawsend();
		char buf[256];
		unsigned x = 20;
		if (net_get_state(&state)) {
			char *ptr = buf;
			unsigned i, n = sizeof buf;
			if (n > col - x) n = col - x;
			i = snprintf(ptr, n, "transfers: %u", state.transfers);
			n -= i; ptr += i;
			if (state.recv[0]) {
				i = snprintf(ptr, n, ", recv: %s", state.recv);
				n -= i;
				ptr += i;
			}
			if (state.send[0]) {
				i = snprintf(ptr, n, ", send: %s", state.send);
				n -= i;
				ptr += i;
			}
			mvaddstr(0, x, buf);
		}
		clrtoeol();
		mvaddch(row - 1, col - 2, yay[i]);
		i ^= 1;
		clrtoeol();
		move(row - 2, textp);
		curs_set(1);
		refresh();
		while ((key = getch()) != ERR) {
			if (key == KEY_RESIZE) {
				reshape();
				continue;
			}
			if (key == KEY_F(2)) {
				running = 0;
				break;
			} else if (key == KEY_F(3))
				goto_menu(menu == M_MAIN ? M_FILE : M_MAIN);
			else
				kbp(key);
		}
	}
unlock:
	if (pthread_mutex_unlock(&gevlock) != 0)
		abort();
	uifree();
	return 0;
}

static void goto_menu(unsigned m)
{
	if (menu == m) return;
	if (m == M_FILE) {
		ls_free(&ls);
		io_filei = 0;
		if (ls_init(&ls, ".") && ls_init(&ls, "/")) {
			strperror("map pwd");
			return;
		}
		io_select = ls.n;
		statusf("items: %zu", ls.n);
	}
	menu = m;
	// XXX consider saving/restoring in own menu specific buffer
	text[textp = 0] = '\0';
	dirty = 0;
	reshape();
}
