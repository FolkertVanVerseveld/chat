#include "string.h"

char *strncpyz(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n - 1);
	dest[n - 1] = '\0';
	return dest;
}

unsigned long strhash(const char *str)
{
	unsigned long hash = 5381;
	int c;
	while (c = *str++)
		hash = (hash << 5) + hash + c;
	return hash;
}
