#include "text.h"
#include "ui.h"
#include "view.h"
#include <ncurses.h>

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
