#include "text.h"
#include "ui.h"
#include "view.h"
#include "string.h"
#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <pthread.h>
#include <sys/time.h>

static WINDOW *scr = NULL;

void txtgetdim(int *y, int *x)
{
	int yp, xp;
	getmaxyx(scr, yp, xp);
	*y = yp;
	*x = xp;
}

int txtinit(void)
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
	txtfree();
	fprintf(stderr, "%s\n", err);
	return 1;
}

void txtfree(void)
{
	ls_free(&ls);
	if (scr) {
		delwin(scr);
		scr = NULL;
	}
	endwin();
}

void txthdr(void)
{
	mvaddstr(0, 0, menu == M_MAIN ? "F2:quit F3:send" : "F2:quit F3:cancel");
	refresh();
}

int txtmain(void)
{
	struct timeval time;
	struct timespec delta;
	int key, running = 0;
	const char *yay = "+-";
	running = 1;
	if (pthread_mutex_lock(&gevlock) != 0)
		abort();
	struct net_state old_state, state;
	state.transfers = 1;
	state.tries = 0;
	state.send[0] = state.recv[0] = '\0';
	unsigned n = state.transfers;
	struct timespec old, now;
	clock_gettime(CLOCK_MONOTONIC, &old);
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
		now = old;
#ifndef LAZY_UPDATE
		net_get_state(&state);
#else
		if (net_get_state(&state)) {
#endif
			long diff = 0;
			if (n == state.transfers)
				clock_gettime(CLOCK_MONOTONIC, &now);
			char *ptr = buf;
			char pdone[80], ptot[80], speed[40];
			float perc;
			unsigned i, n = sizeof buf;
			if (n > col - x) n = col - x;
			i = snprintf(ptr, n, "transfers: %u", state.transfers);
			n -= i; ptr += i;
			if (state.recv[0]) {
				strtosi(pdone, sizeof pdone, state.ar_off, 3);
				strtosi(ptot, sizeof ptot, state.ar_size, 3);
				perc = state.ar_size ? state.ar_off * 100.0f / state.ar_size : 100.0f;
				diff = state.ar_off - old_state.ar_off;
				streta(speed, sizeof speed, old, now, diff);
				i = snprintf(ptr, n, ", recv: %s %s/%s (%.2f%%) %s", state.recv, pdone, ptot, perc, speed);
				n -= i;
				ptr += i;
			}
			if (state.send[0]) {
				strtosi(pdone, sizeof pdone, state.as_off, 3);
				strtosi(ptot, sizeof ptot, state.as_size, 3);
				perc = state.as_size ? state.as_off * 100.0f / state.as_size : 100.0f;
				diff = state.as_off - old_state.as_off;
				streta(speed, sizeof speed, old, now, diff);
				i = snprintf(ptr, n, ", send: %s %s/%s (%.2f%%) %s", state.send, pdone, ptot, perc, speed);
				n -= i;
				ptr += i;
			}
			mvaddstr(0, x, buf);
#ifdef LAZY_UPDATE
		}
#endif
		n = state.transfers;
		old = now;
		old_state = state;
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
	txtfree();
	return 0;
}
