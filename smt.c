#include <smt/smt.h>
#include <stdio.h>
#include <stdlib.h>
#include "font_rgb.h"
#include "view.h"

#define GLYPH_WIDTH 9
#define GLYPH_HEIGHT 16

#define TWIDTH COL_MIN
#define THEIGHT (2 * ROW_MIN)
#define WIDTH (GLYPH_WIDTH * TWIDTH)
#define HEIGHT (GLYPH_HEIGHT * THEIGHT)
#define TITLE "chat"

#define stub fprintf(stderr,"%s: stub\n",__func__)

static unsigned win = SMT_RES_INVALID;
static unsigned gl = SMT_RES_INVALID;

static GLuint tex;

static unsigned t_attr;

static char scrtext[THEIGHT][TWIDTH];
static unsigned scrattr[THEIGHT][TWIDTH];

static const GLubyte palette[COL_MAX][3] = {
	[COL_BLACK  ] = {0  ,   0,   0},
	[COL_RED    ] = {255,   0,   0},
	[COL_GREEN  ] = {  0, 255,   0},
	[COL_YELLOW ] = {255, 255,   0},
	[COL_BLUE   ] = {  0,   0, 255},
	[COL_MAGENTA] = {255,   0, 255},
	[COL_CYAN   ] = {  0, 255, 255},
	[COL_WHITE  ] = {255, 255, 255},
};

static void putstr(unsigned y, unsigned x, const char *str)
{
	char *pos, *end;
	unsigned *attr = (unsigned*)scrattr + y * TWIDTH + x;
	pos = (char*)scrtext + y * TWIDTH + x;
	end = (char*)scrtext + THEIGHT * TWIDTH;
	while (*str && pos < end) {
		*pos++ = *str++;
		*attr++ = t_attr;
	}
}

static void cleanup(void)
{
	glDeleteTextures(1, &tex);
	if (gl != SMT_RES_INVALID) {
		smtFreegl(gl);
		gl = SMT_RES_INVALID;
	}
	if (win != SMT_RES_INVALID) {
		smtFreewin(win);
		win = SMT_RES_INVALID;
	}
}

static int init(void)
{
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	GLint format = FONT_FORMAT;
	glTexImage2D(GL_TEXTURE_2D, 0, format, FONT_WIDTH, FONT_HEIGHT, 0, format, FONT_TYPE, font_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return 0;
}

static void drawtext(void)
{
	GLfloat x0, y0, x1, y1, tx0, ty0, tx1, ty1;
	unsigned y, x, ch;
	const char *str = (const char*)scrtext;
	unsigned *attr = (unsigned*)scrattr;
	const GLubyte *col;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// background
	glBegin(GL_QUADS);
	for (y = 0; y < THEIGHT; ++y) {
		for (x = 0; x < TWIDTH; ++x) {
			col = &palette[*attr++ >> 4][0];
			glColor3ub(col[0], col[1], col[2]);
			y0 = (float)y * GLYPH_HEIGHT;
			y1 = (float)y * GLYPH_HEIGHT + GLYPH_HEIGHT;
			x0 = (float)x * GLYPH_WIDTH;
			x1 = (float)x * GLYPH_WIDTH + GLYPH_WIDTH;
			glVertex2f(x0, y0);
			glVertex2f(x1, y0);
			glVertex2f(x1, y1);
			glVertex2f(x0, y1);
		}
	}
	glEnd();
	// foreground
	glColor3ub(255, 255, 255);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glBegin(GL_QUADS);
	for (y = 0; y < THEIGHT; ++y) {
		for (x = 0; x < TWIDTH; ++x) {
			ch = *str++;
			ty0 = 1.0f / 16 * (ch / 16);
			ty1 = 1.0f / 16 * (1 + ch / 16);
			tx0 = 1.0f / 16 * (ch % 16);
			tx1 = 1.0f / 16 * (1 + ch % 16);
			y0 = (float)y * GLYPH_HEIGHT;
			y1 = (float)y * GLYPH_HEIGHT + GLYPH_HEIGHT;
			x0 = (float)x * GLYPH_WIDTH;
			x1 = (float)x * GLYPH_WIDTH + GLYPH_WIDTH;
			glTexCoord2f(tx0, ty0); glVertex2f(x0, y0);
			glTexCoord2f(tx1, ty0); glVertex2f(x1, y0);
			glTexCoord2f(tx1, ty1); glVertex2f(x1, y1);
			glTexCoord2f(tx0, ty1); glVertex2f(x0, y1);
		}
	}
	glEnd();
	glDisable(GL_TEXTURE_2D);
}

static void display(void)
{
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, WIDTH, HEIGHT, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	drawtext();
}

static inline void glcheck(void)
{
	GLenum err;
	if ((err = glGetError()) != GL_NO_ERROR)
		fprintf(stderr, "gl oops: %x\n", err);
}

int smtinit(void)
{
	int ret = 1, argc = 1;
	char *argv[2] = {TITLE, NULL};
	if (smtInit(&argc, argv))
		goto fail;
	atexit(cleanup);
	ret = smtCreatewin(&win, WIDTH, HEIGHT, TITLE, SMT_WIN_VISIBLE | SMT_WIN_BORDER);
	if (ret) goto fail;
	ret = smtCreategl(&gl, win);
	if (ret) goto fail;
	ret = init();
fail:
	return ret;
}

static int fkey(unsigned key)
{
	if (key == 27)
		return 1;
	return 0;
}

int smtmain(void)
{
	unsigned running = 1;
	while (running) {
		unsigned ev;
		while ((ev = smtPollev()) != SMT_EV_DONE) {
			switch (ev) {
				case SMT_EV_QUIT: goto end;
				case SMT_EV_KEY_DOWN:
					if (fkey(smt.kbp.virt))
						goto end;
					break;
			}
		}
		display();
		glcheck();
		smtSwapgl(win);
	}
end:
	return 0;
}

void smthdr(void)
{
	putstr(0, 0, menu == M_MAIN ? "F3:send" : "F3:cancel");
}
