#include "net.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "fs.h"
#include "ui.h"
#include "serpent.h"
#include "string.h"

int net_run = 0;
int net_fd = -1;
static int net_salt = 0;
static serpent_ctx ctx;

#define F_ACTIVE 1
#define F_START 2

struct f_send {
	uint64_t size;
	uint8_t state;
	char name[FNAMESZ];
};

static unsigned f_count = 0;
static struct f_send f_q[IOQSZ];
static pthread_mutex_t f_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned ar_i = 256;
static char ar_name[FNAMESZ];
static unsigned as_i = 256;
static char as_name[FNAMESZ];

#define LOCK if(pthread_mutex_lock(&f_lock))abort()
#define UNLOCK if(pthread_mutex_unlock(&f_lock))abort()

static const uint16_t nt_ltbl[NT_MAX + 1] = {
	[NT_ACK ] = 0,
	[NT_ERR ] = 0,
	[NT_NACK] = 0,
	[NT_DONE] = 0,
	[NT_EHLO] = PASSSZ,
	[NT_SALT] = N_SALTSZ,
	[NT_TEXT] = N_TEXTSZ,
	[NT_FHDR] = N_FHDRSZ,
	[NT_FBLK] = N_FBLKSZ,
};

int pkgout(struct npkg *pkg, int fd)
{
	uint16_t length, t_length;
	uint8_t *ptr = (uint8_t*)pkg;
	ssize_t n;
	assert(pkg->type <= NT_MAX);
	t_length = nt_ltbl[pkg->type];
	length = t_length + N_HDRSZ;
	pkg->length = htobe16(length);
	pkg->chksum = ptr[0] + ptr[1] + ptr[2] + ptr[3] + ptr[4];
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
	uint8_t sum, *ptr = (uint8_t*)pkg;
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
	sum = ptr[0] + ptr[1] + ptr[2] + ptr[3] + ptr[4];
	if (sum != pkg->chksum) {
		// XXX consider don't send packet back
		netcommerr(net_fd, pkg, NE_SUM);
		return NS_ERR;
	}
	if (length < 4 || length > sizeof(struct npkg)) {
		uierrorf("impossibru: length=%u", length);
		return NS_ERR;
	}
	if (pkg->type > NT_MAX) {
		uierrorf("bad type: type=%u", pkg->type);
		return NS_ERR;
	}
	t_length = nt_ltbl[pkg->type];
	n = pkgread(fd, &pkg->data, t_length);
	if (n == -1 || n != t_length) {
		uierrorf("impossibru: n=%zu", n);
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
	// TODO memset pkg here using computed pkg->length to improve performance
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
	case NE_SUM: uierror("bad packet or authentication"); break;
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

static int send_start(struct npkg *p)
{
	int ret = 1;
	if (p->code != NT_FHDR) {
		netcommerr(net_fd, p, NE_TYPE);
		return 1;
	}
	unsigned slot = p->id;
	LOCK;
	if (sq_start(slot))
		goto fail;
	f_q[slot].state |= F_START;
	ret = 0;
fail:
	UNLOCK;
	return ret;
}

static int send_abort(struct npkg *p)
{
	if (p->code != NT_FHDR) {
		netcommerr(net_fd, p, NE_TYPE);
		return 1;
	}
	unsigned slot = p->id;
	LOCK;
	if (!(f_q[slot].state & (F_ACTIVE | F_START))) {
		netcommerr(net_fd, p, NE_TYPE);
		UNLOCK;
		return 1;
	}
	--f_count;
	f_q[slot].state = 0;
	UNLOCK;
	return 0;
}

static void file_done(uint8_t id)
{
	LOCK;
	// ignore status for now
	--f_count;
	ar_i = 256;
	ar_name[0] = '\0';
	as_i = 256;
	as_name[0] = '\0';
	f_q[id].state = 0;
	UNLOCK;
}

static int file_data(struct npkg *p)
{
	unsigned slot = p->id;
	struct npkg pkg;
	int ret = 1;
	memset(&pkg, 0, sizeof pkg);
	LOCK;
	// reject if slot is invalid
	if (!(f_q[slot].state & (F_ACTIVE | F_START))) {
		pkginit(&pkg, NT_NACK);
		pkg.code = NT_FHDR;
		pkg.id = slot;
		pkgout(&pkg, net_fd);
		goto fail;
	}
	// grab data and let fs handle it
	uint64_t offset, size;
	offset = be64toh(p->data.fblk.offset);
	size = be64toh(p->data.fblk.size);
	ret = rq_data(slot, p->data.fblk.blk, offset, size);
	if (ret)
		f_q[slot].state = 0;
	// update state
	if (slot != ar_i) {
		ar_i = slot;
		strncpyz(ar_name, f_q[slot].name, FNAMESZ);
	}
fail:
	UNLOCK;
	return ret;
}

static int file_recv(struct npkg *p)
{
	struct npkg pkg;
	const char *name = p->data.fhdr.name;
	uint64_t size = be64toh(p->data.fhdr.st_size);
	unsigned slot = p->data.fhdr.id;
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_NACK);
	pkg.code = NT_FHDR;
	pkg.id = slot;
	if (rq_put(name, size, slot))
		goto fail;
	pkginit(&pkg, NT_ACK);
	LOCK;
	struct f_send *f = &f_q[slot];
	f->size = size;
	f->state = F_ACTIVE | F_START;
	++f_count;
	strncpyz(f->name, name, FNAMESZ);
	UNLOCK;
fail:
	// acknowledge/reject request to send file
	return pkgout(&pkg, net_fd);
}

int comm_handle(int sock, struct npkg *pkg)
{
	switch (pkg->type) {
	case NT_ACK:
		return send_start(pkg);
	case NT_NACK:
		return send_abort(pkg);
	case NT_ERR:
		netperror(pkg->code);
		close(sock);
		goto fail;
	case NT_TEXT:
		uitext(pkg->data.text);
		break;
	case NT_FHDR:
		return file_recv(pkg);
	case NT_FBLK:
		return file_data(pkg);
	case NT_DONE:
		file_done(pkg->id);
		break;
	default:
		netcommerr(sock, pkg, NE_TYPE);
		close(sock);
		goto fail;
	}
	return 0;
fail:
	return 1;
}

int net_file_send(const char *name, uint64_t size, uint8_t *slot)
{
	struct npkg pkg;
	int ret = 1;
	LOCK;
	unsigned i, low, up;
	if (cfg.mode & MODE_SERVER) {
		low = 0;
		up = IOQSZ / 2;
	} else {
		low = IOQSZ / 2;
		up = IOQSZ;
	}
	// naively search empty slot
	for (i = low; i < up; ++i)
		if (!(f_q[i].state & F_ACTIVE))
			break;
	if (i == up)
		goto fail;
	// send request to send file
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_FHDR);
	pkg.data.fhdr.st_size = htobe64(size);
	pkg.data.fhdr.id = i;
	strncpyz(pkg.data.fhdr.name, name, FNAMESZ);
	ret = pkgout(&pkg, net_fd);
	if (ret) goto fail;
	// occupy slot
	struct f_send *f = &f_q[i];
	f->size = size;
	f->state |= F_ACTIVE;
	++f_count;
	strncpyz(f->name, name, FNAMESZ);
	*slot = i;
	as_i = i;
	strncpyz(as_name, name, FNAMESZ);
	ret = 0;
fail:
	UNLOCK;
	return ret;
}

int net_file_done(uint8_t id)
{
	struct npkg pkg;
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_DONE);
	pkg.id = id;
	file_done(id);
	return pkgout(&pkg, net_fd);
}

int net_file_data(uint8_t id, const void *data, uint64_t offset, unsigned n)
{
	struct npkg pkg;
	assert(n <= FBLKSZ);
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_FBLK);
	pkg.id = id;
	pkg.data.fblk.offset = htobe64(offset);
	pkg.data.fblk.size = htobe64(n);
	memcpy(pkg.data.fblk.blk, data, n);
	LOCK;
	struct f_send *f = &f_q[id];
	if ((f->state & (F_ACTIVE | F_START)) != (F_ACTIVE | F_START)) {
		UNLOCK;
		return 1;
	}
	int ret = pkgout(&pkg, net_fd);
	if (ret)
		f->state &= ~F_START;
	UNLOCK;
	return ret;
}

int net_get_state(struct net_state *state)
{
	unsigned n = state->transfers;
#ifdef LAZY_STATUS
	if (pthread_mutex_trylock(&f_lock)) {
		if (++state->tries == 10) {
			state->tries = 0;
			// force acquire lock
			LOCK;
		} else
			return 0;
	}
#else
	LOCK;
#endif
	state->transfers = f_count;
	if (ar_i < 256)
		strncpyz(state->recv, ar_name, FNAMESZ);
	else
		state->recv[0] = '\0';
	if (as_i < 256)
		strncpyz(state->send, as_name, FNAMESZ);
	else
		state->send[0] = '\0';
	state->ar_i = ar_i;
	state->as_i = as_i;
	UNLOCK;
	return n != state->transfers;
}
