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
#define NT_MAX 4

#define N_HDRSZ 16
#define N_SALTSZ 64
#define N_TEXTSZ 256

extern int net_run; // XXX subject to race conditions if int is nonatomic
extern int net_fd;

struct npkg {
	uint16_t length;
	uint8_t prot, type, code, chksum;
	uint8_t res[10]; // currently used for alignment
	union {
		struct {
			uint8_t size;
			char key[PASSSZ - 1]; // *not* \0 terminated!
		} ehlo;
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

void ctx_init(const void *salt, size_t n);

#define nschk(x) \
	if (x != NS_OK) {\
		switch (x) {\
		case NS_LEFT:uierror("other left");goto fail;\
		default:uierrorf("network error: code %u",x);goto fail;\
		}\
	}

#endif
