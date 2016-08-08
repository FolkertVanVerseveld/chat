#include <stdio.h>
#include <string.h>
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

static void *netmain(void *arg)
{
	(void)arg;
	struct npkg pkg;
	net_run = 1;
	uistatus("wait");
	cl = sizeof(struct sockaddr_in);
	client = accept(ssock, (struct sockaddr*)&ca, (socklen_t*)&cl);
	if (client < 0) {
		uiperror("accept");
		goto end;
	}
	uistatus("accept");
	while (net_run) {
		memset(&pkg, 0, sizeof pkg);
		int ns = pkgin(&pkg, client);
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
