#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>
#include <endian.h>

#define PORT 25659
#define BACKLOG 1

#define NS_OK 0
#define NS_LEFT 1
#define NS_PROT 2
#define NS_ERR 3

#define NT_ACK 0
#define NT_ERR 1
#define NT_EHLO 2
#define NT_SALT 3
#define NT_MAX 3

#define N_HDRSZ 8

extern int net_run; // XXX subject to race conditions if int is nonatomic

struct npkg {
	uint16_t length;
	uint8_t prot, type;
	uint8_t res[4]; // currently used for alignment
	union {
		uint8_t ack;
	} data;
};

void pkginit(struct npkg *pkg, uint8_t type);
int pkgout(struct npkg *pkg, int fd);
int pkgin(struct npkg *pkg, int fd);

int noclaim(int fd);
int netehlo(int fd);

#endif
