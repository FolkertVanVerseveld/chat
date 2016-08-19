#ifndef CONFIG_H
#define CONFIG_H

#define PORT 25659
#define BACKLOG 1

#define PASSSZ 256

#define CFG_FNAME "chat.cfg"

struct cfg {
	unsigned mode;
	uint16_t port;
	const char *address;
	const char *fname;
	char pass[PASSSZ];
};

extern struct cfg cfg;

#endif
