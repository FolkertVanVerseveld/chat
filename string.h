#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <string.h>
#include <sys/time.h>

/* equivalent to strncpy but guaranteed to be \0 terminated */
char *strncpyz(char *dest, const char *src, size_t n);
/* adjust src to make sure last characters fit in dest and
prefix dest with elipsis if src does not fit in dest */
char *strencpyz(char *dest, const char *src, size_t n, const char *elipsis);
unsigned long strhash(const char *str);
unsigned strtosi(char *str, size_t n, size_t num, unsigned fnum);
void streta(char *str, size_t n, struct timespec start, struct timespec now, long diff);

#endif
