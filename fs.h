#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <dirent.h>

struct ls {
	struct dirent **list;
	size_t n;
	char *path;
};

int fs_init(void);
void fs_free(void);
int ls_init(struct ls *this, const char *name);
int ls_cd(struct ls *this, const char *path);
int d_isdir(struct dirent *d);
void ls_free(struct ls *this);
// Send Queue put
int sq_put(const char *path, const char *name, uint64_t *size);
int sq_start(uint8_t id);
// Receive Queue put
int rq_put(const char *name, uint64_t size, uint8_t id);
int rq_data(uint8_t id, const void *data, uint64_t offset, unsigned n);

#endif
