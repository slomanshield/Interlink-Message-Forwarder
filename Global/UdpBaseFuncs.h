#ifndef UDP_BASE_FUNCS_H
#define UDP_BASE_FUNCS_H

#include "global.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/un.h>


class UdpBaseFuncs
{
	typedef struct sockaddr_in sockaddr_data;
	public:
		UdpBaseFuncs();
		~UdpBaseFuncs();
		int CloseFd(int* socketFd);
		int Bind(char* multiCastGroup, uint16_t port, int* outSocketFd, int* outEpollBindFd, bool nonBlock);
		int CreateDgramSocket(bool nonBlock,int* outSocketFd);
		int SetIOBufferSize(int socketFd, size_t recvBytes, size_t sendBytes);
		int DeleteSocketFromEpoll(int socketFd, int epollFd);
		int GetSocketError(int socketFd);
		int SendToMultiCast(char* multiCastGroup, int socketFd,uint16_t port, unsigned char* buffer, uint64_t length);
		int RecvFrom(int socketFd, unsigned char* buffer, uint64_t inLength, uint64_t* lengthOut);
	private:
		int CreateBindEpollFd(int* listenSocketFd, int* outEpollBindFd);
};

#endif