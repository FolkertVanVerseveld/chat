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
#define NT_MAX 3

#define N_HDRSZ 16
#define N_SALTSZ 64

extern int net_run; // XXX subject to race conditions if int is nonatomic

struct npkg {
	uint16_t length;
	uint8_t prot, type, code;
	uint8_t res[11]; // currently used for alignment
	union {
		struct {
			uint8_t size;
			char key[PASSSZ - 1]; // *not* \0 terminated!
		} ehlo;
		uint8_t salt[N_SALTSZ];
	} data;
};

void pkginit(struct npkg *pkg, uint8_t type);
int pkgout(struct npkg *pkg, int fd);
int pkgin(struct npkg *pkg, int fd);

#define NE_TYPE 1
#define NE_KEY 2

int noclaim(int fd);
int netcommerr(int fd, struct npkg *pkg, int code);
void netperror(int code);

#define nschk(x) \
	if (x != NS_OK) {\
		switch (x) {\
		case NS_LEFT:uistatus("other left");goto fail;\
		default:uistatusf("network error: code %u",x);goto fail;\
		}\
	}

#endif
