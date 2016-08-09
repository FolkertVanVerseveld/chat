#include "net.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "ui.h"
#include "serpent.h"

int net_run = 0;
int net_fd = -1;
static int net_salt = 0;
static serpent_ctx ctx;

static const uint16_t nt_ltbl[NT_MAX + 1] = {
	[NT_ACK] = 0,
	[NT_ERR] = 0,
	[NT_EHLO] = PASSSZ,
	[NT_SALT] = N_SALTSZ,
	[NT_TEXT] = N_TEXTSZ,
};

int pkgout(struct npkg *pkg, int fd)
{
	uint16_t length, t_length;
	ssize_t n;
	assert(pkg->type <= NT_MAX);
	t_length = nt_ltbl[pkg->type];
	length = t_length + N_HDRSZ;
	pkg->length = htobe16(length);
	if (net_salt) {
		struct npkg crypt;
		serpent_encblk(&ctx, pkg, &crypt, length);
		memcpy(pkg, &crypt, length);
	}
	while (length) {
		n = send(fd, pkg, length, 0);
		if (!n) return NS_LEFT;
		if (n < 0) return NS_ERR;
		length -= n;
	}
	return NS_OK;
}

static char pb_data[UINT16_MAX];
static uint16_t pb_size = 0;

static ssize_t pkgread(int fd, void *buf, uint16_t n)
{
	ssize_t length;
	size_t need;
	char *dst = buf;
	if (pb_size) {
		uint16_t off;
		// use remaining data
		// if everything is buffered already
		if (pb_size >= n)
			goto copy;
		// we need to copy all buffered data and
		// wait for the next stuff to arrive
		memcpy(buf, pb_data, off = pb_size);
		pb_size = 0;
		for (need = n - pb_size; need; need -= length, pb_size += length) {
			length = recv(fd, &pb_data[pb_size], need, 0);
			if (length <= 0) return length;
		}
		dst += off;
		goto copy;
	}
	pb_size = 0;
	for (need = n; need; need -= length, pb_size += length) {
		length = recv(fd, &pb_data[pb_size], need, 0);
		if (length <= 0) return length;
	}
copy:
	memcpy(buf, pb_data, pb_size > n ? n : pb_size);
	if (pb_size > n)
		memmove(pb_data, &pb_data[pb_size], UINT16_MAX - pb_size);
	pb_size -= n;
	return n;
}

int pkgin(struct npkg *pkg, int fd)
{
	ssize_t n;
	uint16_t length, t_length;
	n = pkgread(fd, pkg, N_HDRSZ);
	if (!n) return NS_LEFT;
	if (n == -1 || n != N_HDRSZ) return NS_ERR;
	if (net_salt) {
		struct npkg crypt;
		serpent_decblk(&ctx, pkg, &crypt, N_HDRSZ);
		memcpy(pkg, &crypt, N_HDRSZ);
	}
	length = be16toh(pkg->length);
	if (length < 4 || length > sizeof(struct npkg)) {
		uierrorf("impossibru: length=%u\n", length);
		return NS_ERR;
	}
	if (pkg->type > NT_MAX) {
		uierrorf("bad type: type=%u\n", pkg->type);
		return NS_ERR;
	}
	t_length = nt_ltbl[pkg->type];
	n = pkgread(fd, &pkg->data, t_length);
	if (n == -1 || n != t_length) {
		uierrorf("impossibru: n=%zu\n", n);
		return NS_ERR;
	}
	if (net_salt) {
		struct npkg crypt;
		serpent_decblk(&ctx, &pkg->data, &crypt.data, t_length);
		memcpy(&pkg->data, &crypt.data, t_length);
	}
	length -= N_HDRSZ;
	if (length - N_HDRSZ > t_length)
		return NS_ERR;
	return NS_OK;
}

void pkginit(struct npkg *pkg, uint8_t type)
{
	assert(type <= NT_MAX);
	pkg->type = type;
	pkg->prot = 0;
	pkg->length = htobe16(nt_ltbl[type] + N_HDRSZ);
}

int noclaim(int fd)
{
	register int level, optname;
	int optval;
	level = SOL_SOCKET;
	optname = SO_REUSEADDR;
	optval = 1;
	return setsockopt(fd, level, optname, &optval, sizeof(int));
}

int netcommerr(int fd, struct npkg *p, int code)
{
	struct npkg pkg;
	switch (code) {
	case NE_TYPE: uierrorf("bad pkg type: %hhu", p->type); break;
	case NE_KEY: uierror("bad authentication"); break;
	default: uierrorf("network error: code %d", code); break;
	}
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_ERR);
	pkg.code = (uint8_t)(code & 0xff);
	return pkgout(&pkg, fd);
}

void netperror(int code)
{
	switch (code) {
	case NE_KEY:
		uierror("authentication failed");
		break;
	default:
		uierrorf("fatal error occurred: code %d", code);
		break;
	}
}

int nettext(const char *text)
{
	struct npkg pkg;
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_TEXT);
	strncpy(pkg.data.text, text, N_TEXTSZ);
	pkg.data.text[N_TEXTSZ - 1] = '\0';
	return pkgout(&pkg, net_fd);
}

void ctx_init(const void *salt, size_t n)
{
	if (n > 256) n = 256;
	serpent_init(&ctx, salt, n);
	net_salt = 1;
}
