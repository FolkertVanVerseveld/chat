#include <smt/smt.h>
#include <stdio.h>
#include <stdlib.h>
#include "font_rgb.h"
#include "view.h"

#define GLYPH_WIDTH 9
#define GLYPH_HEIGHT 16

#define WIDTH (GLYPH_WIDTH * COL_MIN)
#define HEIGHT (GLYPH_HEIGHT * 2 * ROW_MIN)
#define TITLE "chat"

static unsigned win = SMT_RES_INVALID;
static unsigned gl = SMT_RES_INVALID;

static GLuint tex;

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

static void display(void)
{
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, WIDTH, HEIGHT, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glColor3ub(255, 255, 255);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(FONT_WIDTH, 0);
	glTexCoord2f(1, 1); glVertex2f(FONT_WIDTH, FONT_HEIGHT);
	glTexCoord2f(0, 1); glVertex2f(0, FONT_HEIGHT);
	glEnd();
	glDisable(GL_TEXTURE_2D);
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
	if (ret) goto fail;
	display();
	glcheck();
	smtSwapgl(win);
	ret = 0;
fail:
	return ret;
}
