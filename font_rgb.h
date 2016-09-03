#ifndef FONT_H
#define FONT_H

/* bitmap of default VGA font in qemu */

#include <GL/gl.h>

#define FONT_WIDTH 144
#define FONT_HEIGHT 256
#define FONT_FORMAT GL_RGB
#define FONT_TYPE GL_UNSIGNED_SHORT_5_6_5

extern const unsigned char font_data[FONT_WIDTH * FONT_HEIGHT * 2 + 1];

#endif
