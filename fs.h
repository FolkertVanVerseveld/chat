#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <sys/types.h>
#include <dirent.h>

struct ls {
	struct dirent **list;
	size_t n;
	char *path;
};

int ls_init(struct ls *this, const char *name);
int ls_cd(struct ls *this, const char *path);
int d_isdir(struct dirent *d);
void ls_free(struct ls *this);

#endif
