// Copyright Epic Games, Inc. All Rights Reserved.

#include <netinet/tcp.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "../Common/Public/UbaLinuxNetworkWrappers.h"

int UnixGetsockopt(int fd, int level, int optname, char* optval, int* optlen)
{
	return getsockopt(fd, level, optname, optval, optlen);
}

int UnixSetsockopt(int fd, int level, int optname, char* optval, int optlen)
{
	return setsockopt(fd, level, optname, optval, optlen);
}

int UnixGetTcpInfo(int fd, void* buf, int* len)
{
	socklen_t l = *len;
	if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, buf, &l) == -1)
		return -errno;

	*len = (int)l;
	return 0;
}
