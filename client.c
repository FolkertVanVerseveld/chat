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

static void *netmain(void *arg)
{
	(void)arg;
	struct npkg pkg;
	net_run = 1;
	uistatus("connected");
	while (net_run) {
		memset(&pkg, 0, sizeof pkg);
		int ns = pkgin(&pkg, sock);
		if (ns != NS_OK) {
			switch (ns) {
			case NS_LEFT:
				uistatus("other left\n");
				net_run = 0;
				break;
			default:
				uistatusf("network error: code %u\n", ns);
				goto end;
			}
		}
	}
end:
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
