#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "server.h"
#include "net.h"

static int ssock = -1, client = -1;
static struct sockaddr_in sa, ca;
static socklen_t cl;
static pthread_t t_net;
static int net_err = 0;

static void *netmain(void *arg)
{
	(void)arg;
	puts("wait");
	cl = sizeof(struct sockaddr_in);
	client = accept(ssock, (struct sockaddr*)&ca, (socklen_t*)&cl);
	if (client < 0) {
		perror("accept");
		net_err = 1;
		return NULL;
	}
	puts("accept");
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
