#include <ctype.h>
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

#define str(x) #x
#define stre(e) str(e)

static int help = 0;

struct cfg cfg = {
	.port = PORT,
	.address = "127.0.0.1",
	.fname = CFG_FNAME,
};

/*
use these options to construct parseable options for getopt(3).
NOTE the first letter has to be unique in order for this to work.
*/
struct opt {
	struct option o;
	const char *desc;
} opts[] = {
	{{"help"   , no_argument, 0, 0}, "this help"},
	{{"server" , no_argument, 0, 0}, "master mode"},
	{{"client" , no_argument, 0, 0}, "slave mode"},
	{{"file"   , required_argument, 0, 0}, "options file location"},
#ifdef DEBUG
	{{"key"    , required_argument, 0, 0}, "password (max " stre(PASSSZ) ")"},
#endif
	{{"port"   , required_argument, 0, 0}, "transfer endpoint number"},
	{{"address", required_argument, 0, 0}, "transfer endpoint IP"},
	{{0, 0, 0, 0}, ""},
};

#define OPTSZ ((sizeof opts)/(sizeof opts[0]))

static struct option long_opt[OPTSZ];
static char opt_help[1024];
static char opt_buf[256];

/* construct parseable options for getopt(3) and create help information */
static void opt_init(int argc, char **argv)
{
	char *h_ptr = opt_help, *o_ptr = opt_buf;
	h_ptr += sprintf(h_ptr,
		"Chat program\n"
		"usage: %s OPTIONS\n"
		"available options:\n"
		"ch long    description\n",
		argc > 0 ? argv[0] : "chat"
	);
	for (unsigned i = 0; i < OPTSZ - 1; ++i){
		struct opt *opt = &opts[i];
		long_opt[i] = opt->o;
		h_ptr += sprintf(h_ptr, " %c %7s %s\n", opt->o.name[0], opt->o.name, opt->desc);
		*o_ptr++ = opt->o.name[0];
		int has_arg = opt->o.has_arg;
		if (has_arg == required_argument)
			*o_ptr++ = ':';
		else if (has_arg == optional_argument) {
			*o_ptr++ = ':';
			*o_ptr++ = ':';
		}
	}
}

static void usage(int help)
{
	fputs(opt_help, help ? stdout : stderr);
	help = 1;
}

static int cfg_read(const char *path);

static int parse_opt(int argc, char **argv)
{
	int c, o_i;
	while (1) {
		c = getopt_long(argc, argv, opt_buf, long_opt, &o_i);
		if (c == -1) break;
		switch (c) {
		case 'h':
			usage(1);
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
			strncpyz(cfg.address, optarg, ADDRSZ);
			break;
		case 'k': {
			strncpyz(cfg.pass, optarg, PASSSZ);
			unsigned long hash;
			hash = strhash(cfg.pass);
			ctx_init(&hash, sizeof hash);
			break;
		}
		case 'f':
			if (cfg_read(cfg.fname = optarg)) {
				perror(optarg);
				return -1;
			}
			break;
		}
	}
	return o_i;
}

#define OPT_TOKSZ 32
#define OPT_VALSZ PASSSZ

static int cfg_read(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 1;
	char *line = NULL, *ptr;
	ssize_t zd;
	size_t n;
	int ret = 1;
	char token[OPT_TOKSZ], value[OPT_VALSZ];
	while ((zd = getline(&line, &n, f)) >= 0) {
		if (zd > 0)
			line[zd - 1] = '\0';
		ptr = line;
		while (*ptr && isspace(*ptr))
			++ptr;
		if (!*ptr || *ptr == '#')
			continue;
		int count;
		if ((count = sscanf(ptr, "%"stre(OPT_TOKSZ)"s = %"stre(OPT_VALSZ)"s", token, value)) != 2) {
			fprintf(stderr, "bad line or assignment: %s\n", line);
			goto fail;
		}
		if (!strcmp(token, "key"))
			strncpyz(cfg.pass, value, OPT_VALSZ);
		else if (!strcmp(token, "port")) {
			int port = atoi(value);
			if (port < 1 || port > 65535) {
				fprintf(stderr, "%s: bad port, use 1-65535\n", value);
				goto fail;
			}
			cfg.port = port;
		} else if (!strcmp(token, "address"))
			strncpyz(cfg.address, value, OPT_VALSZ);
	}
	ret = 0;
fail:
	if (line) free(line);
	fclose(f);
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	opt_init(argc, argv);
	ret = parse_opt(argc, argv);
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
