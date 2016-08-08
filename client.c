#include <stdio.h>
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
static int net_err = 0;

static void *netmain(void *arg)
{
	(void)arg;
	puts("connecting");
	if (connect(sock, (struct sockaddr*)&sa, sizeof sa) < 0) {
		perror("connect");
		net_err = 1;
		return NULL;
	}
	puts("connected");
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
	if (pthread_cancel(t_net) != 0) {
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
