#ifndef TCP_BASE_FUNCS_H
#define TCP_BASE_FUNCS_H

#include "global.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <fcntl.h>

class TcpBaseFuncs
{
	typedef struct sockaddr_in sockaddr_data;
	public:
		TcpBaseFuncs();
		~TcpBaseFuncs();
		int Connect(char* address, uint16_t port, int* outSocketFd, bool nonBlock);
		int CloseFd(int* fd);
		int Bind(uint16_t port, int* outSocketFd, int* outEpollBindFd, int backlog, bool nonBlock, char* overrideIp = nullptr);
		int Accept(int socketFd, int* outConnFd,int epollFd,int timeoutMilli,std::string* peerAddressOut, bool* timeout, bool nonBlock);
		int SetIOBufferSize(int socketFd, size_t recvBytes, size_t sendBytes);
		int DeleteSocketFromEpoll(int socketFd,int epollFd);
		int AddSocketToEpoll(int socketFd, int epollFd, struct epoll_event* event);
		int GetSocketError(int socketFd);
		int SetEpollEventOption(int socketFd, int epollFd, struct epoll_event* event);
		int IsWritable(int socketFd, int epollFd, bool* connected, struct epoll_event* event);
		int Send(int socketFd, unsigned char* buffer, uint64_t length,uint64_t* lengthSent);
		int Recv(int socketFd, unsigned char* buffer, uint64_t inLength, uint64_t* lengthOut);
		int GetHostIp(std::string* addressOut);
	private:
		int CreateBindEpollFd(int* listenSocketFd, int* outEpollBindFd);
		int MakeSocketNonBlocking(int socketFd);
};

#endif