#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "client.h"
#include "ui.h"
#include "net.h"

static int sock = -1;
static struct sockaddr_in sa;
static pthread_t t_net;
static uint8_t salt[N_SALTSZ];

static int netehlo(int fd)
{
	struct npkg pkg;
	memset(&pkg, 0, sizeof pkg);
	uistatus("authenticating...");
	pkginit(&pkg, NT_EHLO);
	size_t len = strlen(cfg.pass);
	memcpy(pkg.data.ehlo.key, cfg.pass, PASSSZ - 1);
	pkg.data.ehlo.size = len > UINT8_MAX ? UINT8_MAX : len;
	return pkgout(&pkg, fd);
}

static void netsalt(struct npkg *pkg)
{
	memcpy(salt, pkg->data.salt, N_SALTSZ);
	ctx_init(salt, N_SALTSZ);
	uistatus("connected to server");
}

static void *netmain(void *arg)
{
	(void)arg;
	struct npkg pkg;
	int ns;
	net_run = 1;
	uistatus("connected");
	if ((ns = netehlo(sock)) != NS_OK) {
		switch (ns) {
		case NS_LEFT:
			uierror("other left unexpectedly");
			goto fail;
		default:
			uierrorf("network error or authentication failure: code %u", ns);
			goto fail;
		}
	}
	while (net_run) {
		memset(&pkg, 0, sizeof pkg);
		ns = pkgin(&pkg, sock);
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
		case NT_SALT:
			netsalt(&pkg);
			break;
		default:
			if (comm_handle(sock, &pkg))
				goto fail;
		}
	}
fail:
	net_run = 0;
	return NULL;
}

int cmain(void)
{
	int domain = AF_INET, type = SOCK_STREAM, prot = IPPROTO_TCP;
	int ret = 1;
	sock = socket(domain, type, prot);
	if (sock == -1) {
		perror("socket");
		goto fail;
	}
	sa.sin_addr.s_addr = inet_addr(cfg.address);
	sa.sin_family = domain;
	sa.sin_port = htobe16(cfg.port);
	if (connect(sock, (struct sockaddr*)&sa, sizeof sa) < 0) {
		perror("connect");
		goto fail;
	}
	net_fd = sock;
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
	if (sock != -1)
		close(sock);
	return ret;
}
