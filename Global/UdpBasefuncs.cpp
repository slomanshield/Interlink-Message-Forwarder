#include "UdpBaseFuncs.h"

UdpBaseFuncs::UdpBaseFuncs()
{
	return;
}

UdpBaseFuncs::~UdpBaseFuncs()
{
	return;
}

int UdpBaseFuncs::Bind(char* multiCastGroup, uint16_t port, int* outSocketFd, int* outEpollBindFd, bool nonBlock)
{
	int cc = 0;
	sockaddr_data serv_addr = { 0 };
	struct ip_mreq group = { 0 };

	serv_addr.sin_addr.s_addr = INADDR_ANY;/* listen on all ips*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (nonBlock == true)
		*outSocketFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	else
		*outSocketFd = socket(AF_INET, SOCK_DGRAM, 0);

	if (*outSocketFd != -1)
	{
		/* only 1 instance of this is allowed per system port should be managed at caller level */
		cc = bind(*outSocketFd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));

		if (cc != 0)
		{
			*outSocketFd = 0;
			cc = errno;
			printf("Error UdpBaseFuncs::Bind, calling bind %d: %s \n",
				cc, strerror(cc));
		}
		
	}
	else
	{
		cc = errno;
		printf("Error UdpBaseFuncs::Bind, calling socket %d: %s \n",
			cc, strerror(cc));
	}

	if (cc == 0)
	{
		group.imr_multiaddr.s_addr = inet_addr(multiCastGroup);
		group.imr_interface.s_addr = INADDR_ANY;

		cc = setsockopt(*outSocketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group, sizeof(group));

		if (cc != 0)
		{
			cc = errno;
			printf("Error UdpBaseFuncs::Bind, calling setsockopt(IP_ADD_MEMBERSHIP) %d: %s \n",
				cc, strerror(cc));
			CloseFd(outSocketFd);
			*outSocketFd = 0;
		}
		
	}

	/* Ok we are listening lets set up that epoll */
	if (cc == 0)
		cc = CreateBindEpollFd(outSocketFd, outEpollBindFd);


	return cc;
}

int UdpBaseFuncs::CreateDgramSocket(bool nonBlock, int* outSocketFd)
{
	int cc = 0;
	struct in_addr localInterface;
	localInterface.s_addr = INADDR_ANY;

	if (nonBlock == true)
		*outSocketFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	else
		*outSocketFd = socket(AF_INET, SOCK_DGRAM, 0);

	if (*outSocketFd == -1)
	{
		cc = errno;
		printf("Error UdpBaseFuncs::socket, calling socket %d: %s \n",
			cc, strerror(cc));
		*outSocketFd = 0;
	}
	else
	{
		cc = setsockopt(*outSocketFd, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface));
		if (cc != 0)
		{
			cc = errno;
			printf("Error UdpBaseFuncs::Bind, calling setsockopt(IP_MULTICAST_IF) %d: %s \n",
				cc, strerror(cc));
			CloseFd(outSocketFd);
			*outSocketFd = 0;
		}
	}

	return cc;
}

int UdpBaseFuncs::SetIOBufferSize(int socketFd, size_t recvBytes, size_t sendBytes)
{
	int cc = 0;

	cc = setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, &recvBytes, sizeof(size_t));

	if (cc == -1)
	{
		cc = errno;
		printf("Error UdpBaseFuncs::SetIOBufferSize, calling setsockopt(RCV) %d: %s \n",
			cc, strerror(cc));
	}

	if (cc == 0)
	{
		cc = setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, &sendBytes, sizeof(size_t));
	}

	if (cc == -1)
	{
		cc = errno;
		printf("Error UdpBaseFuncs::SetIOBufferSize, calling setsockopt(SND) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

/* This should only be used if you are moving a socketfd from 1 epoll to another
	If CloseFd is called it is removed from the epollFd no clean up required */
int UdpBaseFuncs::DeleteSocketFromEpoll(int socketFd, int epollFd)
{
	int cc = 0;

	cc = epoll_ctl(epollFd, EPOLL_CTL_DEL, socketFd, NULL);

	if (cc == -1)
	{
		cc = errno;
		printf("Error UdpBaseFuncs::DeleteSocketFromEpoll, calling epoll_ctl(EPOLL_CTL_DEL) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int UdpBaseFuncs::GetSocketError(int socketFd)
{
	int cc = 0;
	int code = 0;
	socklen_t len = sizeof(int);

	cc = getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &code, &len);

	if (cc == 0)
		cc = code;

	return cc;
}

int UdpBaseFuncs::CloseFd(int* fd)
{
	int cc = 0;

	cc = close(*fd);

	if (cc == -1)
	{
		cc = errno;
		printf("Error UdpBaseFuncs::CloseFd, calling close %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int UdpBaseFuncs::CreateBindEpollFd(int* listenSocketFd, int* outEpollBindFd)
{
	int cc = 0;
	struct epoll_event accept_event;
	accept_event.data.fd = *listenSocketFd;
	accept_event.events = EPOLLIN;

	*outEpollBindFd = epoll_create1(0);

	if (*outEpollBindFd != -1)
	{
		cc = epoll_ctl(*outEpollBindFd, EPOLL_CTL_ADD, *listenSocketFd, &accept_event);
		if (cc != 0)
		{
			cc = errno;
			printf("Error UdpBaseFuncs::CreateBindEpollFd, calling epoll_ctl %d: %s \n",
				cc, strerror(cc));
		}
	}
	else
	{
		*outEpollBindFd = 0;
		cc = errno;
		printf("Error UdpBaseFuncs::CreateBindEpollFd, calling epoll_create1 %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int UdpBaseFuncs::SendToMultiCast(char* multiCastGroup, int socketFd,uint16_t port, unsigned char* buffer, uint64_t length)
{
	int cc = 0;
	ssize_t sizeSent = 0;
	struct sockaddr_in msg_addr;
	socklen_t addr_len = sizeof(msg_addr);
	msg_addr.sin_family = AF_INET;
	msg_addr.sin_addr.s_addr = inet_addr(multiCastGroup);
	msg_addr.sin_port = htons(port);

	sizeSent = sendto(socketFd, buffer, length, MSG_NOSIGNAL, (struct sockaddr*)&msg_addr, addr_len);

	if (sizeSent == -1)
	{
		cc = errno;
		if (cc == EAGAIN || cc == EWOULDBLOCK)
			cc = ENOBUFS;// force it to ENOBUFS because in nonblocking mode it means the buffer is full
		printf("Error UdpBaseFuncs::SendToMultiCast, calling send %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int UdpBaseFuncs::RecvFrom(int socketFd, unsigned char* buffer, uint64_t inLength, uint64_t* lengthOut)
{
	int cc = 0;
	struct sockaddr_in peer_addr = { 0 };
	ssize_t dataLength = 0;
	socklen_t peer_addr_len = sizeof(peer_addr);

	dataLength = recvfrom(socketFd, buffer, inLength, 0, (struct sockaddr*)&peer_addr, &peer_addr_len);

	if (dataLength == -1)
	{
		cc = errno;
		if (cc == EAGAIN || cc == EWOULDBLOCK)// fake success for non blocking socket if there is no data
		{
			cc = 0;
			*lengthOut = 0;
		}
		else
		{
			printf("Error TcpBaseFuncs::Send, calling send %d: %s \n",
				cc, strerror(cc));
		}
	}
	else
		*lengthOut = dataLength;

	return cc;
}