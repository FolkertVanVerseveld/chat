#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>
#include <endian.h>
#include "config.h"

#define NS_OK 0
#define NS_LEFT 1
#define NS_PROT 2
#define NS_ERR 3

#define NT_ACK 0
#define NT_ERR 1
#define NT_EHLO 2
#define NT_SALT 3
#define NT_TEXT 4
#define NT_FHDR 5
#define NT_NACK 6
#define NT_FBLK 7
#define NT_DONE 8
#define NT_MAX 8

#define N_HDRSZ 16
#define N_SALTSZ 64
#define N_TEXTSZ 256
#define N_FHDRSZ 256
#define N_FBLKSZ (FBLKSZ+16)

extern int net_run; // XXX subject to race conditions if int is nonatomic
extern int net_fd;

struct npkg {
	uint16_t length;
	uint8_t prot, type, code, chksum, id;
	uint8_t res[9]; // currently used for alignment
	union {
		struct {
			uint8_t size;
			char key[PASSSZ - 1]; // *not* \0 terminated!
		} ehlo;
		struct {
			uint64_t st_size;
			uint8_t id;
			char name[FNAMESZ]; // *not* \0 terminated!
		} fhdr;
		struct {
			uint64_t offset, size;
			char blk[FBLKSZ];
		} fblk;
		char text[N_TEXTSZ];
		uint8_t salt[N_SALTSZ];
	} data;
};

void pkginit(struct npkg *pkg, uint8_t type);
int pkgout(struct npkg *pkg, int fd);
int pkgin(struct npkg *pkg, int fd);

#define NE_TYPE 1
#define NE_KEY 2
#define NE_SUM 3

int noclaim(int fd);
int netcommerr(int fd, struct npkg *pkg, int code);
void netperror(int code);
int nettext(const char *text);

int net_file_send(const char *name, uint64_t size, uint8_t *slot);
int net_file_data(uint8_t id, const void *data, uint64_t offset, unsigned n, uint64_t size);
int net_file_done(uint8_t id);

struct net_state {
	unsigned transfers;
	unsigned tries;
	unsigned as_i, ar_i;
	uint64_t as_size, ar_size;
	uint64_t as_off, ar_off;
	char send[FNAMESZ];
	char recv[FNAMESZ];
};

int net_get_state(struct net_state *state);

/* handle packets that are interpreted the same for master and slave */
int comm_handle(int fd, struct npkg *pkg);

void ctx_init(const void *salt, size_t n);

#define nschk(x) \
	if (x != NS_OK) {\
		switch (x) {\
		case NS_LEFT:uierror("other left");goto fail;\
		default:uierrorf("network error: code %u",x);goto fail;\
		}\
	}

#endif
