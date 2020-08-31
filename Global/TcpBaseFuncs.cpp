#include "TcpBaseFuncs.h"


TcpBaseFuncs::TcpBaseFuncs()
{
	return;
}

TcpBaseFuncs::~TcpBaseFuncs()
{
	return;
}

int TcpBaseFuncs::Connect(char* address, uint16_t port,int* outSocketFd,bool nonBlock)
{
	int cc = 0;
	sockaddr_data serv_addr = { 0 };
	struct addrinfo hints = { 0 };
	struct addrinfo *servinfo = nullptr;
	struct sockaddr_in* reslovedHost = nullptr;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;  //ipv4 only

	cc = getaddrinfo(address, NULL, &hints, &servinfo);

	if (cc == 0)
	{
		reslovedHost = ((struct sockaddr_in*)servinfo->ai_addr);
		char* pAddress = inet_ntoa(reslovedHost->sin_addr); // just use the first should be our only host name (internal network)

		memcpy(&serv_addr.sin_addr.s_addr, &reslovedHost->sin_addr, sizeof(in_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(port);

		freeaddrinfo(servinfo);//no longer needed after copy

		if (nonBlock == true)
			*outSocketFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		else
			*outSocketFd = socket(AF_INET, SOCK_STREAM, 0);

		if (*outSocketFd != -1)
		{
			cc = connect(*outSocketFd, (struct sockaddr *)&serv_addr, sizeof(sockaddr_data));
			if (cc != 0)
			{
				cc = errno;
				if (cc == EINPROGRESS)//socket is nonblocking set to 0 and fall out
					cc = 0;
				else
				{
					printf("Error TcpBaseFuncs::Connect, calling connect %d: %s \n",
						    cc, strerror(cc));
					CloseFd(outSocketFd);
					*outSocketFd = 0;
				}
			}
		}
		else
		{
			cc = errno;
			printf("Error TcpBaseFuncs::Connect, calling socket %d: %s \n",
				cc, strerror(cc));
			*outSocketFd = 0;
		}

	}
	else
	{
		if (cc == EAI_SYSTEM)
		{
			cc = errno;
			printf("Error TcpBaseFuncs::Connect, calling getaddrinfo %d: %s \n",
				    cc, strerror(cc));
		}
		else
		{
			printf("Error TcpBaseFuncs::Connect, calling getaddrinfo %d: %s \n",
				    cc, gai_strerror(cc));
		}
	}

	return cc;
}

/* can be called without calling delete from epoll since it gets removed */
int TcpBaseFuncs::CloseFd(int* fd)
{
	int cc = 0;

	cc = close(*fd);

	if (cc == -1)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::CloseFd, calling close %d: %s \n",
			    cc, strerror(cc));
	}
	
	return cc;
}

int TcpBaseFuncs::Bind(uint16_t port, int* outSocketFd, int* outEpollBindFd, int backlog, bool nonBlock, char* overrideIp)
{
	int cc = 0;
	sockaddr_data serv_addr = { 0 };
	struct addrinfo hints = { 0 };
	struct addrinfo *servinfo = nullptr;
	struct sockaddr_in* reslovedHost = nullptr;
	char hostName[MAX_HOSTNAME_LEN + FOR_NULL] = { 0 };
	std::string address = "";

	hints.ai_family = AF_INET;  //ipv4 only

	if(overrideIp == nullptr)
		cc = gethostname(hostName, sizeof(hostName));

	if (cc == 0)
	{
		if(overrideIp == nullptr)
			cc = getaddrinfo(hostName, NULL, &hints, &servinfo);
		if (cc == 0)
		{
			if (overrideIp == nullptr)
			{
				reslovedHost = ((struct sockaddr_in*)servinfo->ai_addr);

				/* Lets find a non loop back ip.. if its all we got.. O well */
				do
				{
					address = inet_ntoa(reslovedHost->sin_addr);
					/* just look at first 3 chars */
					if (strncmp(address.c_str(), "127", 3) != 0)//found a non loopback lets use it
					{
						reslovedHost = ((struct sockaddr_in*)servinfo->ai_addr);
						break;
					}
					servinfo = servinfo->ai_next;
				} while (servinfo != nullptr);

				memcpy(&serv_addr.sin_addr.s_addr, &reslovedHost->sin_addr, sizeof(in_addr));
			}
			else
				serv_addr.sin_addr.s_addr = inet_addr(overrideIp);
			
			serv_addr.sin_family = AF_INET;
			serv_addr.sin_port = htons(port);

			if (overrideIp == nullptr)
				freeaddrinfo(servinfo);//no longer needed after copy

			if (nonBlock == true)
				*outSocketFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
			else
				*outSocketFd = socket(AF_INET, SOCK_STREAM, 0);

			if (*outSocketFd != -1)
			{
				int one = 1;

				setsockopt(*outSocketFd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
				cc = bind(*outSocketFd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));

				if (cc == 0)
				{
					cc = listen(*outSocketFd, backlog);
					if (cc != 0)
					{
						cc = errno;
						printf("Error TcpBaseFuncs::Bind, calling listen %d: %s \n",
							cc, strerror(cc));
						CloseFd(outSocketFd);
						*outSocketFd = 0;
					}
				}
				else
				{
					cc = errno;
					printf("Error TcpBaseFuncs::Bind, calling bind %d: %s \n",
						cc, strerror(cc));
					CloseFd(outSocketFd);
					*outSocketFd = 0;
				}
			}
			else
			{
				cc = errno;
				printf("Error TcpBaseFuncs::Bind, calling socket %d: %s \n",
					cc, strerror(cc));
				*outSocketFd = 0;
			}

		}
		else
		{
			if (cc == EAI_SYSTEM)
			{
				cc = errno;
				printf("Error TcpBaseFuncs::Bind, calling getaddrinfo %d: %s \n",
					    cc, strerror(cc));
			}
			else
			{
				printf("Error TcpBaseFuncs::Bind, calling getaddrinfo %d: %s \n",
					cc, gai_strerror(cc));
			}
		}
	}
	else
	{
		cc = errno;
		printf("Error TcpBaseFuncs::Bind, calling gethostname %d: %s \n",
			    cc, strerror(cc));
	}

	/* Ok we are listening lets set up that epoll */
	if (cc == 0)
		cc = CreateBindEpollFd(outSocketFd, outEpollBindFd);
	

	return cc;
}

int TcpBaseFuncs::CreateBindEpollFd(int* listenSocketFd, int* outEpollBindFd)
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
			printf("Error TcpBaseFuncs::CreateBindEpollFd, calling epoll_ctl %d: %s \n",
				cc, strerror(cc));
		}
	}
	else
	{
		*outEpollBindFd = 0;
		cc = errno;
		printf("Error TcpBaseFuncs::CreateBindEpollFd, calling epoll_create1 %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int TcpBaseFuncs::MakeSocketNonBlocking(int socketFd)
{
	int cc = 0;

	int flags = fcntl(socketFd, F_GETFL, 0);
	if (flags == -1) 
	{
		cc = errno;
		printf("Error TcpBaseFuncs::MakeSocketNonBlocking, calling fcntl(F_GETFL) %d: %s \n",
			cc, strerror(cc));
	}

	if (fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) == -1) 
	{
		cc = errno;
		printf("Error TcpBaseFuncs::MakeSocketNonBlocking, calling fcntl(F_SETFL) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int TcpBaseFuncs::GetSocketError(int socketFd)
{
	int cc = 0;
	int code = 0;
	socklen_t len = sizeof(int);

	cc = getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &code, &len);

	if (cc == 0)
		cc = code;

	return cc;
}

int TcpBaseFuncs::Accept(int socketFd, int* outConnFd,int epollBindFd,int timeoutMilli,
	                     std::string* peerAddressOut,bool* timeout, bool nonBlock)
{
	int cc = 0;
	int ready = 0;
	struct epoll_event event;
	struct sockaddr_in peer_addr = { 0 };
	socklen_t peer_addr_len = sizeof(peer_addr);

	ready = epoll_wait(epollBindFd, &event, 1, timeoutMilli);

	if (ready == 1)//ok we are good
	{
		*outConnFd = accept(socketFd, (struct sockaddr*)&peer_addr,&peer_addr_len);
		if (*outConnFd != -1)
		{
			*peerAddressOut = inet_ntoa(*((in_addr*)&peer_addr.sin_addr.s_addr));

			if (nonBlock == true)
			{
				cc = MakeSocketNonBlocking(*outConnFd);

				if (cc == -1)
				{
					cc = errno;
					printf("Error TcpBaseFuncs::Accept, calling MakeSocketNonBlocking %d: %s \n",
						cc, strerror(cc));
				}
			}
			
		}
		else
		{
			cc = errno;
			printf("Error TcpBaseFuncs::Accept, calling accept %d: %s \n",
				cc, strerror(cc));
			*outConnFd = 0;
		}
		*timeout = false;
	}
	else if (ready < 0 && errno != EINTR)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::Accept, calling epoll_wait %d: %s \n",
			cc, strerror(cc));
		*outConnFd = 0;
	}
	else
		*timeout = true;

	return cc;
}

int TcpBaseFuncs::SetIOBufferSize(int socketFd, size_t recvBytes, size_t sendBytes)
{
	int cc = 0;
	
	cc = setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, &recvBytes, sizeof(size_t));

	if (cc == -1)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::SetIOBufferSize, calling setsockopt(RCV) %d: %s \n",
			cc, strerror(cc));
	}

	if (cc == 0)
	{
		cc = setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, &sendBytes, sizeof(size_t));
	}

	if (cc == -1)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::SetIOBufferSize, calling setsockopt(SND) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}


int TcpBaseFuncs::DeleteSocketFromEpoll(int socketFd,int epollFd)
{
	int cc = 0;

	cc = epoll_ctl(epollFd, EPOLL_CTL_DEL, socketFd, NULL);

	if (cc == -1)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::DeleteSocketFromEpoll, calling epoll_ctl(EPOLL_CTL_DEL) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int TcpBaseFuncs::AddSocketToEpoll(int socketFd, int epollFd, struct epoll_event* event)
{
	int cc = 0;

	cc = epoll_ctl(epollFd, EPOLL_CTL_ADD, socketFd, event);

	if (cc == -1)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::AddSocketToEpoll, calling epoll_ctl(EPOLL_CTL_ADD) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int TcpBaseFuncs::SetEpollEventOption(int socketFd, int epollFd, struct epoll_event* event)
{
	int cc = 0;

	cc = epoll_ctl(epollFd, EPOLL_CTL_MOD, socketFd, event);

	if (cc == -1)
	{
		cc = errno;
		printf("Error TcpBaseFuncs::SetEpollEventOption, calling epoll_ctl(EPOLL_CTL_MOD) %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}

int TcpBaseFuncs::IsWritable(int socketFd, int epollFd, bool* connected, struct epoll_event* event)
{
	int cc = 0;
	int ready = 0;
	struct epoll_event events = { 0 };
	uint32_t events_mask = EPOLLOUT | EPOLLRDHUP;
	uint32_t events_set = event->events & events_mask;
	*connected = false;

	/* since its 1:1 mapping only 1 event for 1 socket */
	if (events_set == 0) /* if our bits are not set */
	{
		event->events = EPOLLOUT | EPOLLRDHUP;
		cc = SetEpollEventOption(socketFd, epollFd, event);
	}
		
	if (cc == 0)
	{
		ready = epoll_wait(epollFd, &events, 1, 1); /* just sleep for 1 milli event shoudl be active anyway */

		/*  we only want 1 because for a writing socket it should be 1:1
			meaning 1 epollfd per client socket fd for reads it will be different
			and will be handled by the caller since its only 1 call */
		if (ready == 1)
		{
			/* since out and hup can both be set we check both */
			if (events.events & EPOLLOUT)
				*connected = true;

			/* check any errors on the socket */
			if(events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
				*connected = false;
		}
		else if (ready < 0)
		{
			cc = errno;
			printf("Error TcpBaseFuncs::IsWritable, calling epoll_wait %d: %s \n",
				cc, strerror(cc));
			if (cc == EINTR)
				cc = 0;//just have it try again
		}
	}
	
	return cc;
}

int TcpBaseFuncs::Send(int socketFd, unsigned char* buffer, uint64_t length, uint64_t* lengthSent)
{
	int cc = 0;
	ssize_t sizeSent = 0;
	*lengthSent = 0;

	/* we set no signal because we just need return code */
	sizeSent = send(socketFd, buffer, length, MSG_NOSIGNAL);
	
	if (sizeSent == -1)
	{
		cc = errno;
		if (cc == EAGAIN || cc == EWOULDBLOCK)
			cc = ENOBUFS;// force it to ENOBUFS because in nonblocking mode it means the buffer is full

		if(cc != ENOBUFS) /* no real reason to print it out when we are overloading the pipe */
			printf("Error TcpBaseFuncs::Send, calling send %d: %s \n",
				cc, strerror(cc));
	}
	else
		*lengthSent = sizeSent;

	return cc;
}

int TcpBaseFuncs::Recv(int socketFd, unsigned char* buffer, uint64_t inLength, uint64_t* lengthOut)
{
	int cc = 0;
	ssize_t sizeRecv = 0;
	*lengthOut = 0;

	sizeRecv = recv(socketFd, buffer, inLength, 0);

	if (sizeRecv == -1)
	{
		cc = errno;
		if (cc == EWOULDBLOCK)// fake success for non blocking socket if there is no data
		{
			cc = 0;
			*lengthOut = 0;
		}
		else
		{
			printf("Error TcpBaseFuncs::Recv, calling send %d: %s \n",
				cc, strerror(cc));
		}
	}
	else
		*lengthOut = sizeRecv;

	return cc;
}

int TcpBaseFuncs::GetHostIp(std::string* addressOut)
{
	struct addrinfo hints = { 0 };
	struct addrinfo *servinfo = nullptr;
	struct sockaddr_in* reslovedHost = nullptr;
	char hostName[MAX_HOSTNAME_LEN + FOR_NULL] = { 0 };
	int cc = 0;

	hints.ai_family = AF_INET;  //ipv4 only

	cc = gethostname(hostName, sizeof(hostName));

	if (cc == 0)
	{
		cc = getaddrinfo(hostName, NULL, &hints, &servinfo);
		if (cc == 0)
		{
			reslovedHost = ((struct sockaddr_in*)servinfo->ai_addr);

			/* Lets find a non loop back ip.. if its all we got.. O well */
			do
			{
				*addressOut = inet_ntoa(reslovedHost->sin_addr);
				/* just look at first 3 chars */
				if (strncmp(addressOut->c_str(), "127", 3) != 0)//found a non loopback lets use it
					break;

				servinfo = servinfo->ai_next;
			} while (servinfo != nullptr);
		}
		else
		{
			if(cc == EAI_SYSTEM)
				cc = errno;
			printf("Error TcpBaseFuncs::GetHostIp, calling getaddrinfo %d: %s \n",
				cc, strerror(cc));
		}
	}
	else
	{
		cc = errno;
		printf("Error TcpBaseFuncs::GetHostIp, calling gethostname %d: %s \n",
			cc, strerror(cc));
	}

	return cc;
}