#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "server.h"
#include "ui.h"
#include "net.h"

static int ssock = -1, client = -1;
static struct sockaddr_in sa, ca;
static socklen_t cl;
static pthread_t t_net;
static uint8_t salt[N_SALTSZ];

static int netehlo(int fd, struct npkg *p)
{
	struct npkg pkg;
	size_t len;
	len = strlen(cfg.pass);
	if (len != p->data.ehlo.size || memcmp(p->data.ehlo.key, cfg.pass, len))
		return netcommerr(fd, p, NE_KEY);
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_SALT);
	srand(time(NULL));
	for (unsigned i = 0; i < N_SALTSZ; ++i)
		salt[i] = rand();
	memcpy(pkg.data.salt, salt, N_SALTSZ);
	int ret = pkgout(&pkg, fd);
	ctx_init(salt, N_SALTSZ);
	return ret;
}

static void *netmain(void *arg)
{
	(void)arg;
	struct npkg pkg;
	int ns;
	net_run = 1;
	uistatus("waiting for client...");
	cl = sizeof(struct sockaddr_in);
	client = accept(ssock, (struct sockaddr*)&ca, (socklen_t*)&cl);
	if (client < 0) {
		uiperror("accept");
		goto fail;
	}
	net_fd = client;
	uistatus("client connected");
	while (net_run) {
		memset(&pkg, 0, sizeof pkg);
		ns = pkgin(&pkg, client);
		if (ns != NS_OK) {
			switch (ns) {
			case NS_LEFT:
				uierror("other left");
				goto fail;
			default:
				uierrorf("network error: code %u", ns);
				goto fail;
			}
		}
		switch (pkg.type) {
		case NT_ERR:
			netperror(pkg.code);
			close(client);
			goto fail;
		case NT_EHLO:
			ns = netehlo(client, &pkg);
			nschk(ns);
			break;
		case NT_TEXT:
			uitext(pkg.data.text);
			break;
		default:
			netcommerr(client, &pkg, NE_TYPE);
			close(client);
			goto fail;
		}
	}
fail:
	net_run = 0;
	return NULL;
}

int smain(void)
{
	int domain = AF_INET, type = SOCK_STREAM, prot = IPPROTO_TCP;
	int ret = 1;
	ssock = socket(domain, type, prot);
	if (ssock == -1) {
		perror("socket");
		goto fail;
	}
	sa.sin_family = domain;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htobe16(cfg.port);
	if (noclaim(ssock)) {
		perror("noclaim");
		goto fail;
	}
	if (bind(ssock, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
		perror("bind");
		goto fail;
	}
	listen(ssock, BACKLOG);
	if (pthread_create(&t_net, NULL, netmain, NULL) != 0) {
		perror("netmain");
		goto fail;
	}
	ret = uimain();
	if (ret) {
		perror("uimain");
		goto fail;
	}
	ret = 1;
	if (net_run && pthread_cancel(t_net) != 0) {
		perror("pthread_cancel");
		goto fail;
	}
	if (pthread_join(t_net, NULL) != 0) {
		perror("pthread_join");
		goto fail;
	}
	ret = 0;
fail:
	if (client != -1)
		close(client);
	if (ssock != -1)
		close(ssock);
	return ret;
}
