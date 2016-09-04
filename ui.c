#include "ui.h"
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <ncurses.h>
#include "string.h"
#include "net.h"
#include "smt.h"
#include "text.h"
#include "view.h"

// default behavior causes deadlock when run in UI thread
#undef nschk
#define nschk(x) \
	if (x != NS_OK) {\
		switch (x) {\
		case NS_LEFT:strerr("other left");goto fail;\
		default:errorf("network error: code %u",x);goto fail;\
		}\
	}

void histcalc(void)
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
	time(&histt[i]);
	if (histn < HISTSZ)
		++histn;
	else
		histi = (histi + 1) % HISTSZ;
	dirty |= EV_TEXT;
	char log[256 + N_TEXTSZ], *ptr = log;
	struct tm t_now;
	localtime_r(&histt[i], &t_now);
	ptr += strftime(log, sizeof log, "%F %H:%M:%S", &t_now);
	if (hista[i] & HA_OTHER)
		strcpy(ptr, "    ");
	else
		strcpy(ptr, " me ");
	ptr += 4;
	strcpy(ptr, str);
	ptr += strlen(str);
	*ptr = '\0';
	log_txt(log);
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

void wrapaddstr(unsigned y, unsigned x, unsigned d, char *str)
{
	if (d >= col) {
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

void color(unsigned fg, unsigned bg)
{
	int col = bg * COL_COUNT + COL_COUNT - fg - 1;
	if (col < 0) col = 0;
	if (col >= COL_MAX)
		col = COL_MAX;
	setcol(col);
}

static void uigetdim(int *y, int *x)
{
	if (!(cfg.mode & MODE_GUI))
		txtgetdim(y, x);
}

void reshape(void)
{
	int y, x;
	uigetdim(&y, &x);
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
	drawhdr();
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
		uint64_t size;
		if (sq_put(ls.path, e->d_name, &size))
			strerr("send failed");
		else {
			char msg[256];
			snprintf(msg, sizeof msg, "sending file: %s (%zu bytes)", e->d_name, (size_t)size);
			histadd(msg, 0);
			statusf("sending \"%s\"", e->d_name);
		}
	}
	goto_menu(M_MAIN);
	return 1;
}

void kbp(int key)
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
		} else if (menu == M_MAIN)
			t_dirty = kbp_send();
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
	txtfree();
}

static int uiinit(void)
{
	return (cfg.mode & MODE_GUI) ? smtinit() : txtinit();
}

int uimain(void)
{
	if (uiinit())
		return 1;
	drawhdr();
	return viewmain();
}

void goto_menu(unsigned m)
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
