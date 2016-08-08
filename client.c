#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "client.h"
#include "net.h"

static int sock = -1;
static struct sockaddr_in sa;

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
	ret = 0;
fail:
	if (sock != -1)
		close(sock);
	return ret;
}
