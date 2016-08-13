#include "fs.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int ls_init(struct ls *this, const char *name)
{
	int n;
	char *path = strdup(name);
	if (!path) return 1;
	n = scandir(name, &this->list, NULL, alphasort);
	if (n < 0) {
		free(path);
		return 1;
	}
	this->n = n;
	this->path = path;
	return 0;
}

void ls_free(struct ls *this)
{
	if (this->list) {
		for (size_t i = 0; i < this->n; ++i)
			free(this->list[i]);
		free(this->list);
		this->list = NULL;
	}
	if (this->path) {
		free(this->path);
		this->path = NULL;
	}
	this->n = 0;
}

int ls_cd(struct ls *this, const char *path)
{
	size_t len, plen;
	int n;
	len = strlen(path);
	if (!len) return 1;
	plen = strlen(this->path);
	char *new = realloc(this->path, plen + len + 2);
	if (!new) return 1;
	new[plen] = '/';
	strcpy(new + plen + 1, path);
	struct dirent **list;
	n = scandir(new, &list, NULL, alphasort);
	if (n < 0) {
		free(new);
		return 1;
	}
	for (size_t i = 0; i < this->n; ++i)
		free(this->list[i]);
	free(this->list);
	this->list = list;
	this->path = new;
	this->n = n;
	return 0;
}

int d_isdir(struct dirent *d)
{
	if (d->d_type & DT_DIR)
		return 1;
	struct stat st;
	return !stat(d->d_name, &st) && (st.st_mode & S_IFDIR) == S_IFDIR;
}
