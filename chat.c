#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "chat.h"
#include "string.h"
#include "net.h"
#include "server.h"
#include "client.h"

static int help = 0;

struct cfg cfg = {
	.port = PORT,
	.address = "127.0.0.1",
};

static struct option long_opt[] = {
	{"help"   , no_argument, 0, 0},
	{"server" , no_argument, 0, 0},
	{"client" , no_argument, 0, 0},
	{"key"    , required_argument, 0, 0},
	{"port"   , required_argument, 0, 0},
	{"address", required_argument, 0, 0},
	{0, 0, 0, 0}
};

static void usage(void)
{
	fputs(
		"Chat program\n"
		"usage: chat OPTIONS\n"
		"available options:\n"
		"ch long    description\n"
		" h help    this help\n"
		" s server  master mode\n"
		" c client  slave mode\n"
		" k key     password (max 255)\n"
		" p port    transfer endpoint number\n"
		" a address transfer endpoint IP\n",
		help ? stdout : stderr
	);
}

static int parse_opt(int argc, char **argv)
{
	int c, o_i;
	while (1) {
		c = getopt_long(argc, argv, "hscp:a:k:", long_opt, &o_i);
		if (c == -1) break;
		switch (c) {
		case 'h':
			help = 1;
			usage();
			break;
		case 's':
			if (cfg.mode & MODE_CLIENT) {
		err_mode:
				fputs(
					"too many modes: master and slave\n"
					"remove master or slave option\n",
					stderr
				);
				return -1;
			}
			cfg.mode |= MODE_SERVER;
			break;
		case 'c':
			if (cfg.mode & MODE_SERVER)
				goto err_mode;
			cfg.mode |= MODE_CLIENT;
			break;
		case 'p': {
			int port;
			port = atoi(optarg);
			if (port < 1 || port > 65535) {
				fprintf(stderr, "%s: bad port, use 1-65535\n", optarg);
				return -1;
			}
			cfg.port = port;
			break;
		};
		case 'a':
			cfg.address = optarg;
			break;
		case 'k': {
			strncpyz(cfg.pass, optarg, PASSSZ);
			unsigned long hash;
			hash = strhash(cfg.pass);
			ctx_init(&hash, sizeof hash);
			break;
		}
		}
	}
	return o_i;
}

int main(int argc, char **argv)
{
	int ret = parse_opt(argc, argv);
	if (ret < 0) return -ret;
	if (!(cfg.mode & (MODE_SERVER | MODE_CLIENT))) {
		if (help) return 0;
		fputs(
			"bad mode: not master or slave\n"
			"specify -s or -c to start master or slave\n",
			stderr
		);
		return 1;
	}
	if (!cfg.pass[0]) {
		fputs("need authentication: specify key phrase using -k\n", stderr);
		return 1;
	}
	return cfg.mode & MODE_SERVER ? smain() : cmain();
}
