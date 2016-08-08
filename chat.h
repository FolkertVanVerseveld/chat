#ifndef CHAT_H
#define CHAT_H

#include <stdint.h>
#include "config.h"

#define MODE_SERVER 1
#define MODE_CLIENT 2

struct cfg {
	unsigned mode;
	uint16_t port;
	const char *address;
	char pass[PASSSZ];
};

extern struct cfg cfg;

#endif
