// Copyright Epic Games, Inc. All Rights Reserved.

#include <winsock2.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>

// NOTE, README
// Must use "WINAPI" on function and add Windows.def


NTSYSAPI NTSTATUS CDECL wine_server_handle_to_fd( HANDLE handle, unsigned int access, int *unix_fd, unsigned int *options );

extern int UnixGetsockopt(int fd, int level, int optname, char* optval, int* optlen);
extern int UnixSetsockopt(int fd, int level, int optname, char* optval, int optlen);

extern int UnixGetTcpInfo(int fd, void* buf, int* len);
extern bool UnixGetTcpAutoTuning(int* outReadMin, int* outReadDefault, int* outReadMax, int* outWriteMin, int* outWriteDefault, int* outWriteMax);

INT WINAPI GetLinuxTcpInfo(SOCKET s, void* buf, INT* len)
{
	int fd = -1;
	if (wine_server_handle_to_fd((HANDLE)s, 0, &fd, NULL) != 0)
		return -1;
	int res = UnixGetTcpInfo(fd, buf, len);
	close(fd);
	return res;
}

int UNIX_IPPROTO_TCP = 6;
int TCP_CONGESTION = 13;


bool WINAPI GetCongestionAlgorithm(SOCKET s, char* out, int outCapacity)
{
	int fd = -1;
	if (wine_server_handle_to_fd((HANDLE)s, 0, &fd, NULL) != 0)
		return -1;
	int res = UnixGetsockopt(fd, UNIX_IPPROTO_TCP, TCP_CONGESTION, out, &outCapacity);
	close(fd);
	return res == 0;
}

bool WINAPI SetCongestionAlgorithm(SOCKET s, char* value, int valuelen)
{
	int fd = -1;
	if (wine_server_handle_to_fd((HANDLE)s, 0, &fd, NULL) != 0)
		return -1;
	int res = UnixSetsockopt(fd, UNIX_IPPROTO_TCP, TCP_CONGESTION, value, valuelen);
	close(fd);
	return res == 0;
}


bool WINAPI GetTcpAutoTuning(int* outReadMin, int* outReadDefault, int* outReadMax, int* outWriteMin, int* outWriteDefault, int* outWriteMax)
{
	return UnixGetTcpAutoTuning(outReadMin, outReadDefault, outReadMax, outWriteMin, outWriteDefault, outWriteMax);
}