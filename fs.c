#include "fs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "config.h"
#include "net.h"

#define F_ACTIVE 1
#define F_START 2

struct f_item {
	uint64_t size;
	uint64_t offset;
	int fd;
	unsigned state;
	char *map;
} f_q[IOQSZ];

static pthread_t t_send;
static pthread_mutex_t f_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t f_cond = PTHREAD_COND_INITIALIZER;

#define LOCK if(pthread_mutex_lock(&f_lock))abort()
#define UNLOCK if(pthread_mutex_unlock(&f_lock))abort()

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

// NOTE always free(3) returned value if not NULL
static char *strrename(const char *name)
{
	unsigned i, tries;
	size_t len = strlen(name);
	char *dup;
	struct stat st;
	dup = malloc(len + 4);
	if (!dup) goto fail;
	memcpy(dup, name, len + 1);
	for (i = 0, tries = 10; tries; --tries, ++i) {
		snprintf(&dup[len], 4 - 1, "%u", i);
		if (stat(dup, &st) != 0 && errno == ENOENT)
			return dup;
	}
	// XXX consider mktemp if for loop failed
fail:
	if (dup) free(dup);
	return NULL;
}

int rq_put(const char *name, uint64_t size, uint8_t slot)
{
	mode_t mode = 0664;
	int ret = 1, fd = -1;
	struct stat st;
	char *map = MAP_FAILED;
	char *alt = NULL;
	register int oflags = O_CREAT | O_RDWR | O_EXCL;
	fd = open(name, oflags, mode);
	if (fd == -1) {
		if (errno == EPERM || errno == EEXIST) {
			// rename and try again
			alt = strrename(name);
			if (!alt) goto fail;
			name = alt;
			fd = open(name, oflags, mode);
			if (fd != -1) goto resize;
		}
		// try different mode or give up
		mode = 0600;
		fd = open(name, oflags, mode);
		if (fd == -1)
			goto fail;
	}
resize:
	if (posix_fallocate(fd, 0, size))
		goto fail;
	if (fstat(fd, &st) || st.st_size < 0 || (size_t)st.st_size != size)
		goto fail;
	map = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		goto fail;
	if (alt) free(alt);
	// occupy slot
	LOCK;
	struct f_item *f = &f_q[slot];
	f->size = size;
	f->map = map;
	f->fd = fd;
	f->state = F_ACTIVE | F_START;
	UNLOCK;
	ret = 0;
fail:
	if (alt) free(alt);
	if (ret) {
		if (map != MAP_FAILED) munmap(map, size);
		if (fd != -1) close(fd);
	}
	return ret;
}

int sq_start(uint8_t id)
{
	int ret = 1;
	LOCK;
	struct f_item *f = &f_q[id];
	if (!(f->state & F_ACTIVE))
		goto fail;
	f->state |= F_START;
	ret = 0;
	pthread_cond_broadcast(&f_cond);
fail:
	UNLOCK;
	return ret;
}

int sq_put(const char *path, const char *name)
{
	char *fname, *map = MAP_FAILED;
	size_t p_len, n_len, f_len;
	int ret = 1, fd = -1;
	struct stat st;
	p_len = strlen(path);
	n_len = strlen(name);
	f_len = p_len + n_len + 2;
	fname = malloc(f_len);
	if (!fname)
		goto fail;
	sprintf(fname, "%s/%s", path, name);
	fd = open(fname, O_RDONLY);
	if (fd == -1 || fstat(fd, &st))
		goto fail;
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	uint8_t slot;
	ret = net_file_send(name, st.st_size, &slot);
	if (ret)
		goto fail;
	// occupy slot
	LOCK;
	struct f_item *f = &f_q[slot];
	f->size = st.st_size;
	f->fd = fd;
	f->map = map;
	f->state = F_ACTIVE;
	UNLOCK;
fail:
	if (fname) free(fname);
	if (ret) {
		if (map != MAP_FAILED)
			munmap(map, st.st_size);
		if (fd != -1)
			close(fd);
	}
	return ret;
}

static unsigned s_run = 0;

static void *sendmain(void *arg)
{
	(void)arg;
	struct timeval time;
	struct timespec delta;
	s_run = 1;
	while (s_run) {
		struct f_item f;
		LOCK;
		// XXX consider no timedwait and either:
		// * use goto magic to search active slot
		// * write while no active slot loop
		if (gettimeofday(&time, NULL) != 0) {
			UNLOCK;
			goto fail;
		}
		delta.tv_sec = time.tv_sec + 1L;
		delta.tv_nsec = time.tv_usec;
		pthread_cond_timedwait(&f_cond, &f_lock, &delta);
		unsigned i, low, up;
		if (cfg.mode & MODE_SERVER) {
			low = 0;
			up = IOQSZ / 2;
		} else {
			low = IOQSZ / 2;
			up = IOQSZ;
		}
		// naively search active slot
		for (i = low; i < up; ++i)
			if ((f_q[i].state & (F_ACTIVE | F_START)) == (F_ACTIVE | F_START))
				break;
		if (i == up) {
			UNLOCK;
			continue;
		}
		f_q[i].offset = 0;
		f = f_q[i];
		UNLOCK;
		uint64_t diff;
		unsigned aborted = 0;
		char blk[FBLKSZ];
		while (f.offset != f.size) {
			diff = f.size - f.offset;
			if (diff > FBLKSZ) diff = FBLKSZ;
			// FIXME segfault if symlink is send as file
			memcpy(blk, f.map + f.offset, diff);
			if (net_file_data(i, blk, f.offset, diff, f.size)) {
			abort:
				// abort file transfer
				aborted = 1;
				LOCK;
				struct f_item *f = &f_q[i];
				if (f->map != MAP_FAILED) {
					munmap(f->map, f->size);
					f->map = MAP_FAILED;
					f->size = 0;
				}
				if (f->fd != -1) {
					close(f->fd);
					f->fd = -1;
				}
				f->state = 0;
				UNLOCK;
				break;
			}
			f.offset += diff;
		}
		if (!aborted && net_file_done(i))
			goto abort;
	}
fail:
	s_run = 0;
	return NULL;
}

int rq_data(uint8_t id, const void *data, uint64_t offset, unsigned n)
{
	int ret = 1;
	LOCK;
	struct f_item *f = &f_q[id];
	if ((f->state & (F_ACTIVE | F_START)) != (F_ACTIVE | F_START))
		goto fail;
	if (offset + n > f->size)
		goto fail;
	memcpy(f->map + offset, data, n);
	ret = 0;
fail:
	if (ret)
		f->state = 0;
	UNLOCK;
	return ret;
}

int fs_init(void)
{
	for (unsigned i = 0; i < IOQSZ; ++i) {
		f_q[i].fd = -1;
		f_q[i].map = MAP_FAILED;
	}
	return pthread_create(&t_send, NULL, sendmain, NULL) != 0;
}

void fs_free(void)
{
	LOCK;
	for (unsigned i = 0; i < IOQSZ; ++i) {
		struct f_item *f = &f_q[i];
		if (f->map != MAP_FAILED) {
			munmap(f->map, f->size);
			f->map = MAP_FAILED;
			f->size = 0;
		}
		if (f->fd != -1) {
			close(f->fd);
			f->fd = -1;
		}
		f->state = 0;
	}
	UNLOCK;
}
