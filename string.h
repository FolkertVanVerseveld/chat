#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <string.h>

/* equivalent to strncpy but guaranteed to be \0 terminated */
char *strncpyz(char *dest, const char *src, size_t n);
unsigned long strhash(const char *str);

#endif
