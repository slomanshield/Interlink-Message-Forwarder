#include "MessageForwarder.h"

namespace MessageForwarder
{
	MessageForwarder::MessageForwarder(uint16_t port, std::string queue_name, std::string input_queue_name, std::string output_queue_name,
		                               int numSenders, int numReaders, int numConnectionsPerHost, mode iMode, int connectionTimeoutMilli,
		                               uint64_t ioBufferSize,bool manageOutStanding,std::string multiCastGroup)
	{
		this->port = port;
		this->i_input_queue_name = input_queue_name;
		this->i_output_queue_name = output_queue_name;
		this->i_command_queue_name = "command_" + queue_name;
		this->queue_name = queue_name;
		this->numReaders = numReaders;
		this->numSenders = numSenders;
		this->iMode = iMode;
		this->connectionTimeoutMilli = connectionTimeoutMilli;
		this->numConnectionsPerHost = numConnectionsPerHost;
		this->ioBufferSize = ioBufferSize;
		this->manageOutStanding = manageOutStanding;
		udpFd = 0;
		epollUdpFd = 0;
		connReadEpollFd = 0;
		listenSocketFd = 0;
		listenSocketEpollFd = 0;
		fdMapConnOffset = 0;
		shutdown = false;
		numEvents = 0;
		pQueueManager = QueueWrapper::QueueManager::Instance();
		internalThreadTimeoutMilli = THREAD_WAIT_TIMEOUT;
		listenOverrideTcpIP = "";

		if (ioBufferSize < sizeof(TCP_MESSAGE) || ioBufferSize < sizeof(UDP_MULTICAST_PAYLOAD))
			this->ioBufferSize = sizeof(TCP_MESSAGE) + sizeof(UDP_MULTICAST_PAYLOAD);

		if (multiCastGroup.length() == 0)
			this->multiCastGroup = MULTI_CAST_GROUP;
		else
			this->multiCastGroup = multiCastGroup;

		UdpThreadHandler.SetProcessthread(UdpThread);
		UdpCommandProcessThreadHandler.SetProcessthread(UdpCommandProcessThread);
		MessageReaderThreadHandler.SetProcessthread(MessageReaderThread);
		MessageWriterThreadHandler.SetProcessthread(MessageWriterThread);
		MessageListenerThreadHandler.SetProcessthread(MessageListenerThread);

		return;
	}

	MessageForwarder::~MessageForwarder()
	{
		DestroyUdp();
		if (connReadEpollFd != 0)
			TcpBaseFuncs::CloseFd(&connReadEpollFd);
		DestroyTcpServer();

		pQueueManager->DeleteQueue<std::string>(&i_command_queue_name, true);

		return;
	}

	int MessageForwarder::Init()
	{
		int cc = 0;
		int64_t maxOutStanding = MAX_COMMAND_QUEUE_OUTSTANDING;
		int32_t minimumReaders = 1;

		cc = pQueueManager->CreateQueue<std::string>(&i_command_queue_name, &maxOutStanding, &minimumReaders);

		if (cc != QUEUE_SUCCESS && cc != QUEUE_ALREADY_DEFINED)
			return cc;

		if (connReadEpollFd == 0)
		{
			connReadEpollFd = epoll_create1(0);
			if (connReadEpollFd == -1)
			{
				cc = errno;
				printf("Error Init, calling epoll_create1 %d: %s \n ",
					cc, strerror(cc));
			}
		}

		return cc;
	}

	void MessageForwarder::DestroyUdp()
	{
		if (epollUdpFd != 0 && udpFd != 0)
			UdpBaseFuncs::DeleteSocketFromEpoll(udpFd, epollUdpFd);

		if (epollUdpFd != 0)
		{
			UdpBaseFuncs::CloseFd(&epollUdpFd);
			epollUdpFd = 0;
		}
			
		if (udpFd != 0)
		{
			UdpBaseFuncs::CloseFd(&udpFd);
			udpFd = 0;
		}
			
	}

	void MessageForwarder::DestroyTcpServer()
	{
		if (listenSocketFd != 0 && listenSocketEpollFd != 0)
			TcpBaseFuncs::DeleteSocketFromEpoll(listenSocketFd, listenSocketEpollFd);

		if (listenSocketFd != 0)
		{
			TcpBaseFuncs::CloseFd(&listenSocketFd);
			listenSocketFd = 0;
		}
			
		if (listenSocketEpollFd != 0)
		{
			TcpBaseFuncs::CloseFd(&listenSocketEpollFd);
			listenSocketEpollFd = 0;
		}
			
	}

	int MessageForwarder::Start(std::string* tcpListenIP)
	{
		int cc = 0;
		listenOverrideTcpIP = "";

		if (tcpListenIP != nullptr && iMode == server)
			listenOverrideTcpIP = *tcpListenIP;


		/* create our sockets first */
		cc = InitUdp();

		if (cc == 0 && iMode == server)
			cc = InitTcpServer();

		/* Start processing threads 2nd */
		if (cc == 0)
			cc = StartProcessingThreads();

		/* then start the tcp server */
		if (cc == 0 && iMode == server)
			cc = StartTcpServer();

		if (cc == 0 && iMode == server)
			shutdown = false; /* tell clients to connect to us again */

		/* then udp comands */
		if (cc == 0)
			cc = StartUdp();

		return cc;
	}

	void MessageForwarder::BeginStop()
	{
		if (iMode == server)
			shutdown = true;
	}

	int MessageForwarder::Stop(int timeoutMilli, bool waitForOutStanding , bool forceServerClose)
	{
		int cc = 0;
		int numRemoved = 0;

		BeginStop(); /* just call blindly doesn't do anything for client */

		if(iMode == client) /* we need to stop udp threads first because  they will create new connections */
			cc = StopUdp();

		if (waitForOutStanding == true)
			cc = WaitForOutStandingMessages(timeoutMilli);

		if (cc == 0 && iMode == client)
			numRemoved = RemoveConnections();

		if (cc == 0 && numRemoved == 0 && iMode == client) /* we are succcessful but removed no connections? .. fishy  */
		{
			std::unique_lock lockMaps(m_Maps);
			size_t num_connections = socketConnMap.size();
			lockMaps.unlock();

			if (num_connections > 0)
			{
				printf("Error: still have %lu outstanding connections.. aborting Stop() \n", num_connections);
				cc = connections_not_closed;
			}
		}

		if (iMode == server) /* A server should never cut connections on graceful shutdown lets check */
		{
			int numConnections = CheckNumConnections();

			if (numConnections > 0 && forceServerClose == false)
				cc = connections_exist_on_shutdown;
			else if(numConnections > 0) /* if we want to force close connections */
				RemoveConnections();
		}

		/* ok now lets stop all of our threads */
		if (cc == 0)
		{
			if(iMode == server) /* server mode we want to stop udp threads close to last because we want all the clients to know we are shutting down */
				cc = StopUdp();

			if (cc == 0 && iMode == server)
				cc = StopTcpServer();
			if (cc == 0)
				cc = StopProcessingThreads();
		}

		if (cc == 0) /* once all threads are stopepd then destroy the sockets */
		{
			DestroyUdp();
			DestroyTcpServer();
		}
		
		return cc;
	}

	int MessageForwarder::StartUdp()
	{
		int cc = 0;
		bool started = false;
		
		if (iMode == client)
		{
			started = UdpCommandProcessThreadHandler.StartProcessing(1, this);
			if (started == false)
				cc = failed_starting_udp_command_thread;
		}

		if (cc == 0)
		{
			started = UdpThreadHandler.StartProcessing(1, this);
			if (started == false)
				cc = failed_starting_udp_thread;
		}

		return cc;
	}

	int MessageForwarder::StopUdp()
	{
		int cc = 0;
		bool stopped = false;

		if (iMode == client)
		{
			stopped = UdpCommandProcessThreadHandler.StopProcessing(internalThreadTimeoutMilli + 1000);
			if (stopped == false)
				cc = failed_stopping_udp_command_thread;
		}

		if (cc == 0)
		{
			stopped = UdpThreadHandler.StopProcessing(internalThreadTimeoutMilli + 1000);
			if (stopped == false)
				cc = failed_stopping_udp_command_thread;
		}

		return cc;
	}

	int MessageForwarder::StartTcpServer()
	{
		int cc = 0;
		bool started = false;

		started = MessageListenerThreadHandler.StartProcessing(1, this);
		if (started == false)
			cc = failed_starting_listening_thread;

		return cc;
	}

	int MessageForwarder::StopTcpServer()
	{
		int cc = 0;
		bool stopped = false;

		stopped = MessageListenerThreadHandler.StopProcessing(internalThreadTimeoutMilli + 1000);

		if (stopped == false)
			cc = failed_stopping_listening_thread;

		return cc;
	}

	int MessageForwarder::InitUdp()
	{
		int cc = 0;
		if (udpFd == 0)
		{
			if (iMode == server) /* servers send commands */
				cc = CreateDgramSocket(true, &udpFd);
			else if (iMode == client) /* clients get commands */
				cc = InitUdpServer();
		}
		else
			cc = udp_socket_exists;
		
		return cc;
	}

	int MessageForwarder::InitUdpServer()
	{
		int cc = 0;

		cc = UdpBaseFuncs::Bind((char*)multiCastGroup.c_str(), port, &udpFd, &epollUdpFd, true);

		if (cc == 0)
		{
			cc = UdpBaseFuncs::SetIOBufferSize(udpFd, ioBufferSize, ioBufferSize);
		}

		return cc;
	}

	int MessageForwarder::ReCreateUdpServer()
	{
		int cc = 0;

		printf("MessageForwarder: Recreating udp server socket... \n");
		/* lets re create */
		DestroyUdp();
		cc = InitUdpServer();

		return cc;
	}

	int MessageForwarder::ReCreateUdpClient()
	{
		int cc = 0;

		printf("MessageForwarder: Recreating udp client socket... \n");
		cc = CreateDgramSocket(true, &udpFd);

		return cc;
	}

	int MessageForwarder::ReCreateTcpServer()
	{
		int cc = 0;
		char* pListenOverrideTcpIP = nullptr;

		if (listenOverrideTcpIP.length() > 0)
			pListenOverrideTcpIP = (char *)listenOverrideTcpIP.c_str();

		DestroyTcpServer();
		
		cc = TcpBaseFuncs::Bind(port, &listenSocketFd, &listenSocketEpollFd, numConnectionsPerHost, true, pListenOverrideTcpIP);

		return cc;
	}

	int MessageForwarder::InitTcpServer()
	{
		int cc = 0;
		char* pListenOverrideTcpIP = nullptr;

		if (listenOverrideTcpIP.length() > 0)
			pListenOverrideTcpIP = (char *)listenOverrideTcpIP.c_str();

		if (listenSocketFd == 0)
			/* use numConnectionsPerHost as a back log (antcipate how many connects will come) */
			cc = TcpBaseFuncs::Bind(port, &listenSocketFd, &listenSocketEpollFd, numConnectionsPerHost, true, pListenOverrideTcpIP);
		else
			cc = tcp_server_socket_exists;

		return cc;
	}

	int MessageForwarder::StartProcessingThreads()
	{
		int cc = 0;
		bool started = false;

		started = MessageWriterThreadHandler.StartProcessing(numSenders, this);
		if (started == false)
			cc = failed_starting_processing_threads;
		if (cc == 0)
		{
			started = MessageReaderThreadHandler.StartProcessing(numReaders, this);
			if (started == false)
				cc = failed_starting_processing_threads;
		}
			
		return cc;
	}

	int MessageForwarder::StopProcessingThreads()
	{
		int cc = 0;
		bool stopped = false;

		stopped = MessageWriterThreadHandler.StopProcessing(internalThreadTimeoutMilli + 1000);
		if (stopped == false)
			cc = failed_stopping_processing_threads;
		if (cc == 0)
		{
			stopped = MessageReaderThreadHandler.StopProcessing(internalThreadTimeoutMilli + 1000);
			if (stopped == false)
				cc = failed_stopping_processing_threads;
		}

		return cc;
	}

	int MessageForwarder::InitConnectInfo(CONNECTION_INFO* pConnInfo, char* ipAddress)
	{
		int cc = 0;

		pConnInfo->epollConnFd = epoll_create1(0);
		pConnInfo->peerIpAddress = ipAddress;

		if (pConnInfo->epollConnFd == -1)
		{
			cc = errno;
			printf("Error InitConnectInfo, calling epoll_create1 %d: %s \n ",
				cc, strerror(cc));
		}

		return cc;
	}

	int MessageForwarder::ArmReadConnFd(int sockFd, bool add)
	{
		int cc = 0;
		struct epoll_event event = { 0 };
		event.data.fd = sockFd;
		event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLONESHOT;
		if (add)
			cc = AddSocketToEpoll(sockFd, connReadEpollFd, &event);
		else
			cc = SetEpollEventOption(sockFd, connReadEpollFd, &event);

		if (cc != 0)
			printf("Error: MessageListenerThread (AddSocketToEpoll) error code : %d \n", cc);

		return cc;
	}

	int MessageForwarder::SendData(char* s, uint32_t length, std::string* replyIp, MESSAGE* pMsgIn)
	{
		int cc = 0;
		TCP_MESSAGE tcp_message;

		tcp_message.data_length = length;
		tcp_message.header.fields_length = sizeof(TCP_MESSAGE);

		if (replyIp != nullptr)
			pMsgIn->replyIpAddress = *replyIp;
		else
			pMsgIn->replyIpAddress = "";
		pMsgIn->ss.append((char*)&tcp_message, sizeof(TCP_MESSAGE)); /* first data header */
		pMsgIn->ss.append(s, length); /* then data */

		cc = pQueueManager->PutDataOnQueue<MESSAGE>(&i_output_queue_name, *pMsgIn);

		pMsgIn->ss.clear(); /* clear */

		return cc;
	}

	void MessageForwarder::ExtractUsrData(MESSAGE* pMsg)
	{
		const char* startUsrData = nullptr;
		startUsrData = pMsg->ss.data() + sizeof(TCP_MESSAGE);
		pMsg->ss.assign((char*)startUsrData, pMsg->ss.length() - sizeof(TCP_MESSAGE));
	}

	int MessageForwarder::WaitForConnections(int timeoutMilli)
	{
		int cc = ETIMEDOUT;
		SocketConnMapItr socketMapItr;
		DoubleMili diff;
		double dbTimeoutMilli = timeoutMilli;
		CONNECTION_INFO* pConnInfo = nullptr;
		HighResolutionTimePointClock starTime = HighResolutionTime::now();

		do
		{
			std::shared_lock lockMaps(m_Maps); /* we want to lock each loop so gives a chance for list to be updated */
			for (socketMapItr = socketConnMap.begin(); socketMapItr != socketConnMap.end(); socketMapItr++)
			{
				pConnInfo = (*socketMapItr).second;

				/* if we get into this loop we have at least 1 successful connection next we check if the handshake was done */
				if (pConnInfo->readyToProcess == true) 
				{
					cc = 0;
					break;
				}
			}

			if (cc == 0) /* found a connection that is ready break */
				break;

			usleep(1000);/* sleep for 1 millisecond to give up cpu */

			diff = std::chrono::duration_cast<std::chrono::milliseconds>(HighResolutionTime::now() - starTime);
		} while (diff.count() < dbTimeoutMilli);

		return cc;
	}

	int MessageForwarder::WaitForTcpConnect(ConnInfoList* pConnInfoList)
	{
		int cc = 0;
		HighResolutionTimePointClock starTime = HighResolutionTime::now();
		DoubleMili diff;
		double dbTimeoutMilli = connectionTimeoutMilli;
		bool connected = false;
		CONNECTION_INFO* pConnInfo = nullptr;

		do
		{
			for (ConnInfoListItr itr = pConnInfoList->begin(); itr != pConnInfoList->end(); itr++)
			{
				pConnInfo = (*itr);
				/* we set timeout to 1 milli second so we can get to all of our sockets */
				cc = IsWritable(pConnInfo->connFd, pConnInfo->epollConnFd,
					            &connected, &pConnInfo->eventConn);
				if (cc != 0 || connected == false)
					break;
			}

			/* all sockets connected life is good or error */
			if (cc != 0 || connected == true)
				break;

			usleep(1000);/* sleep for 1 millisecond to give up cpu */

			diff = std::chrono::duration_cast<std::chrono::milliseconds>(HighResolutionTime::now() - starTime);
		}while (diff.count() < dbTimeoutMilli);
		
		if (cc == 0 && connected == false)
			cc = ETIMEDOUT;

		return cc;
	}

	int MessageForwarder::WaitForOutStandingMessages(int timeoutMilli)
	{
		int cc = ETIMEDOUT;
		IpSocketMapItr ipSocketMapItr;
		DoubleMili diff;
		double dbTimeoutMilli = timeoutMilli;
		std::shared_lock lockMaps(m_Maps);
		HighResolutionTimePointClock starTime = HighResolutionTime::now();

		do
		{
			for (ipSocketMapItr = ipSocketMap.begin(); ipSocketMapItr != ipSocketMap.end(); ipSocketMapItr++)
			{
				IP_CONNECTION_INFO* pIpConnInfo = (*ipSocketMapItr).second;
				std::shared_lock lockCountConn(pIpConnInfo->m_outStanding);

				/* still waiting on messages ? */
				if (pIpConnInfo->numOutStanding > 0)
					break;
			}

			/* all entries are done */
			if (ipSocketMapItr == ipSocketMap.end())
			{
				cc = 0;
				break;
			}

			usleep(1000);/* sleep for 1 millisecond to give up cpu */

			diff = std::chrono::duration_cast<std::chrono::milliseconds>(HighResolutionTime::now() - starTime);
		} while (diff.count() < dbTimeoutMilli);

		return cc;
	}

	void MessageForwarder::IncrementOutStanding(IP_CONNECTION_INFO* pIpConnInfo)
	{
		std::unique_lock lock(pIpConnInfo->m_outStanding);
		pIpConnInfo->numOutStanding++;
	}

	void MessageForwarder::DecrementOutStanding(IP_CONNECTION_INFO* pIpConnInfo)
	{
		std::unique_lock lock(pIpConnInfo->m_outStanding);
		pIpConnInfo->numOutStanding--;
	}

	uint32_t MessageForwarder::GetNextConnectionOffset()
	{
		std::unique_lock lock(m_fdMapConnOffset);
		fdMapConnOffset++;
		if (fdMapConnOffset >= socketConnMap.size())
			fdMapConnOffset = 0;
		return fdMapConnOffset;
	}

	uint32_t MessageForwarder::GetNextConnectionOffsetIp(IP_CONNECTION_INFO* ipConnInfo)
	{
		std::unique_lock lock(ipConnInfo->m_ipMapConnOffset);
		ipConnInfo->ipMapConnOffset++;
		if (ipConnInfo->ipMapConnOffset >= ipConnInfo->connInfoMap.size())
			ipConnInfo->ipMapConnOffset = 0;
		return ipConnInfo->ipMapConnOffset;
	}

	int MessageForwarder::WaitForOutStandingMessages(int timeoutMilli,char* ipAddress)
	{
		int cc = ETIMEDOUT;
		IpSocketMapItr ipSocketMapItr;
		SocketConnMapItr socketMapItr;
		DoubleMili diff;
		double dbTimeoutMilli = timeoutMilli;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		std::shared_lock lockMaps(m_Maps);
		HighResolutionTimePointClock starTime = HighResolutionTime::now();

		ipSocketMapItr = ipSocketMap.find(ipAddress);
		if (ipSocketMapItr != ipSocketMap.end())
		{
			do
			{
				pIpConnInfo = (*ipSocketMapItr).second;
				std::shared_lock lockCountConn(pIpConnInfo->m_outStanding);

				/* no more outstanding ? */
				if (pIpConnInfo->numOutStanding == 0)
				{
					cc = 0;
					break;
				}
				
				usleep(1000);/* sleep for 1 millisecond to give up cpu */

				diff = std::chrono::duration_cast<std::chrono::milliseconds>(HighResolutionTime::now() - starTime);
			} while (diff.count() < dbTimeoutMilli);
		}
		else
		{
			printf("Info: WaitForOutStandingMessages(int timeoutMilli,char* ipAddress) ip %s not found on ipSocketMap \n", ipAddress);
			cc = 0;
		}

		return cc;
	}

	int MessageForwarder::AddSocketsToReadFd(ConnInfoList* pConnInfoList)
	{
		int cc = 0;
		CONNECTION_INFO* pConnInfo = nullptr;

		IncreaseEventsCount(0); /* connections are already on the list so just increase by that size */
		for (ConnInfoListItr itr = pConnInfoList->begin(); itr != pConnInfoList->end(); itr++)
		{
			pConnInfo = (*itr);
			cc = ArmReadConnFd(pConnInfo->connFd, true);

			if (cc != 0)
				break;

		}

		return cc;
	}

	int MessageForwarder::CheckNumConnectionsIp(char* ipAddress)
	{
		int numConnections = 0;
		IpSocketMapItr itr;
		std::shared_lock lock(m_Maps);
		
		itr = ipSocketMap.find(ipAddress);

		if (itr != ipSocketMap.end())
		{
			numConnections = (*itr).second->connInfoMap.size();
		}

		return numConnections;
	}

	int MessageForwarder::CheckNumConnections()
	{
		int numConnections = 0;
		std::shared_lock lock(m_Maps);

		numConnections = socketConnMap.size();

		return numConnections;
	}

	void MessageForwarder::AddNewConnections(char* ipAddress, ConnInfoList* pConnInfoList)
	{
		CONNECTION_INFO* pConnInfo = nullptr;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		IpSocketMapItr ipSocketMapItr;
		std::unique_lock lockMaps(m_Maps);

		/* if it doesnt eixst create it */
		ipSocketMapItr = ipSocketMap.find(ipAddress);
		if (ipSocketMapItr == ipSocketMap.end())
			pIpConnInfo = new IP_CONNECTION_INFO;
		else
			pIpConnInfo = (*ipSocketMapItr).second;

		/* lets add these bad boys in */
		for (ConnInfoListItr itr = pConnInfoList->begin(); itr != pConnInfoList->end(); itr++)
		{
			pConnInfo = (*itr);
			/* add it to the ip list */
			pIpConnInfo->connInfoMap.insert({ pConnInfo->connFd, pConnInfo });
			/* add to socket list */
			socketConnMap.insert({ pConnInfo->connFd, pConnInfo });
		}
		
		/* lets check if this is a new ip if so add it */
		ipSocketMapItr = ipSocketMap.find(ipAddress);
		if (ipSocketMapItr == ipSocketMap.end())
			ipSocketMap.insert({ ipAddress, pIpConnInfo });
	}

	int MessageForwarder::AddConnections(char* ipAddress)
	{
		int cc = 0;
		int numConnections = 0;
		int numToConnect = 0;
		CONNECTION_INFO* pConnInfo = nullptr;
		ConnInfoList connInfoList;

		numConnections = CheckNumConnectionsIp(ipAddress);

		if (numConnections < numConnectionsPerHost)/* ok we either dont have enough or non at all */
		{
			numToConnect = numConnectionsPerHost - numConnections;

			/* connect and add to the list */
			for (int i = 0; i < numToConnect; i++)
			{
				pConnInfo = new CONNECTION_INFO;
				connInfoList.push_back(pConnInfo);

				cc = InitConnectInfo(pConnInfo, ipAddress);

				if (cc == 0)
				{
					/* lets do our async connect */
					cc = Connect(ipAddress, port, &pConnInfo->connFd, true);

					if (cc == 0)
						cc = TcpBaseFuncs::SetIOBufferSize(pConnInfo->connFd, ioBufferSize, ioBufferSize);

					if (cc == 0)
					{
						/* now set up event */
						pConnInfo->eventConn.data.fd = pConnInfo->connFd;
						cc = AddSocketToEpoll(pConnInfo->connFd, pConnInfo->epollConnFd, &pConnInfo->eventConn);

						if (cc != 0)
							break;
					}
					else
						break;
				}
				else
					break;
			}

			if (cc == 0)
			{
				/* wait for all sockets to connect */
				cc = WaitForTcpConnect(&connInfoList);

				if (cc == 0)
				{
					/* lets add the sockets to the list */
					AddNewConnections(ipAddress, &connInfoList);

					cc = AddSocketsToReadFd(&connInfoList);
				}
			}
		}
		else
			cc = connections_already_exist;

		/* clean up if we errored out, do not want a memory leak */
		if (cc != 0 && cc != connections_already_exist)
		{
			int numRemoved = RemoveConnections(ipAddress,false);

			/* if we didnt remove any then that means we never called AddNewConnections */
			if (numRemoved <= 0)
			{
				for (ConnInfoListItr itr = connInfoList.begin(); itr != connInfoList.end(); itr++)
				{
					pConnInfo = (*itr);
					TcpBaseFuncs::DeleteSocketFromEpoll(pConnInfo->connFd, pConnInfo->epollConnFd);
					TcpBaseFuncs::CloseFd(&pConnInfo->connFd);
					TcpBaseFuncs::CloseFd(&pConnInfo->epollConnFd);
					delete pConnInfo;
				}

			}
			
		}

		return cc;
	}

	int MessageForwarder::AddConnection(char* ipAddress, int connFd)
	{
		int cc = 0;
		SocketConnMapItr socketMapItr;
		IpSocketMapItr ipSocketMapItr;
		CONNECTION_INFO* pConnInfo = nullptr;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		std::unique_lock lockMaps(m_Maps);

		socketMapItr = socketConnMap.find(connFd);

		if (socketMapItr == socketConnMap.end())
		{
			pConnInfo = new CONNECTION_INFO;
			pConnInfo->connFd = connFd;
			pConnInfo->peerIpAddress = ipAddress;
			if(iMode == server)
				pConnInfo->readyToProcess = true; /* for servers it is ready when created */

			socketConnMap.insert({ pConnInfo->connFd, pConnInfo });

			ipSocketMapItr = ipSocketMap.find(pConnInfo->peerIpAddress);

			if (ipSocketMapItr == ipSocketMap.end())
			{
				pIpConnInfo = new IP_CONNECTION_INFO;
				ipSocketMap.insert({ ipAddress, pIpConnInfo });
			}
			else
				pIpConnInfo = (*ipSocketMapItr).second;

			pIpConnInfo->connInfoMap.insert({ pConnInfo->connFd, pConnInfo });
			
		}
		else
		{
			printf("Info: Connection for FD %d and IP %s already exists \n", pConnInfo->connFd, pConnInfo->peerIpAddress);
			cc = connection_already_exists;
		}

		return cc;
	}

	void MessageForwarder::RemoveConnection(int connFd)
	{
		SocketConnMapItr socketMapItr;
		IpSocketMapItr ipSocketMapItr;
		CONNECTION_INFO* pConnInfo = nullptr;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		std::string ipAddress = "";
		std::unique_lock lockMaps(m_Maps);
		
		socketMapItr = socketConnMap.find(connFd);

		if (socketMapItr != socketConnMap.end())
		{
			pConnInfo = (*socketMapItr).second;
			ipSocketMapItr = ipSocketMap.find(pConnInfo->peerIpAddress);
			ipAddress = pConnInfo->peerIpAddress;
			printf("Info: Closing connection on %s socket %d \n", ipAddress.c_str(), connFd);

			/* lets remove the socket from the socket list */
			if(pConnInfo->epollConnFd != 0) /* server would not have this fd used */
				TcpBaseFuncs::DeleteSocketFromEpoll(pConnInfo->connFd, pConnInfo->epollConnFd);
			TcpBaseFuncs::CloseFd(&pConnInfo->connFd);
			if(pConnInfo->epollConnFd != 0) /* server would not have this fd used */
				TcpBaseFuncs::CloseFd(&pConnInfo->epollConnFd);
			delete pConnInfo;
			socketConnMap.erase(connFd);

			if (ipSocketMapItr != ipSocketMap.end())
			{
				pIpConnInfo = (*ipSocketMapItr).second;
				pIpConnInfo->connInfoMap.erase(connFd);
				
				/* if we have no more connections to this IP we arent getting any messages back */
				if (pIpConnInfo->connInfoMap.size() == 0)
				{
					ipSocketMap.erase(ipAddress);
					delete pIpConnInfo;
				}
			}
			else
				printf("Info: RemoveConnection ip %s not found on ipSocketMap \n", pConnInfo->peerIpAddress.c_str());
		}
		else
			printf("Info: RemoveConnection socket %d not found on socketConnMap \n", connFd);
	}

	int MessageForwarder::RemoveConnections(char* ipAddress, bool removeHostEntry)
	{
		int numRemoved = 0;
		SocketConnMapItr socketMapItr;
		IpSocketMapItr ipSocketMapItr;
		CONNECTION_INFO* pConnInfo = nullptr;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		std::unique_lock lockMaps(m_Maps);

		ipSocketMapItr = ipSocketMap.find(ipAddress);

		if (ipSocketMapItr != ipSocketMap.end())
		{
			pIpConnInfo = (*ipSocketMapItr).second;
			socketMapItr = pIpConnInfo->connInfoMap.begin();

			/* scroll through all the sockets for the host and remove */
			while (socketMapItr != pIpConnInfo->connInfoMap.end())
			{
				pConnInfo = (*socketMapItr).second;
				printf("Info: Closing connection on %s socket %d \n", pConnInfo->peerIpAddress.c_str(), pConnInfo->connFd);
				if (pConnInfo->epollConnFd != 0) /* server would not have this fd used */
					TcpBaseFuncs::DeleteSocketFromEpoll(pConnInfo->connFd, pConnInfo->epollConnFd);
				TcpBaseFuncs::CloseFd(&pConnInfo->connFd);
				if (pConnInfo->epollConnFd != 0) /* server would not have this fd used */
					TcpBaseFuncs::CloseFd(&pConnInfo->epollConnFd);
				socketConnMap.erase(pConnInfo->connFd);
				delete pConnInfo;
				socketMapItr = pIpConnInfo->connInfoMap.erase(socketMapItr);
				numRemoved++;
			}

			/* remove host */
			if (removeHostEntry)
			{
				delete pIpConnInfo;
				ipSocketMap.erase(ipAddress);
			}
			
		}
		else
			printf("Info: RemoveConnections ip %s not found on ipSocketMap \n", ipAddress);

		return numRemoved;
	}

	int MessageForwarder::RemoveConnections()
	{
		int numRemoved = 0;
		SocketConnMapItr socketMapItr;
		IpSocketMapItr ipSocketMapItr;
		CONNECTION_INFO* pConnInfo = nullptr;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		std::string ipAddress = "";
		std::unique_lock lockMaps(m_Maps);

		for (ipSocketMapItr = ipSocketMap.begin(); ipSocketMapItr != ipSocketMap.end(); ipSocketMapItr = ipSocketMap.begin())
		{
			pIpConnInfo = (*ipSocketMapItr).second;
			ipAddress = (*ipSocketMapItr).first;
			socketMapItr = pIpConnInfo->connInfoMap.begin();

			/* scroll through all the sockets for the host and remove */
			while (socketMapItr != pIpConnInfo->connInfoMap.end())
			{
				pConnInfo = (*socketMapItr).second;
				if (pConnInfo->epollConnFd != 0) /* server would not have this fd used */
					TcpBaseFuncs::DeleteSocketFromEpoll(pConnInfo->connFd, pConnInfo->epollConnFd);
				TcpBaseFuncs::CloseFd(&pConnInfo->connFd);
				if (pConnInfo->epollConnFd != 0) /* server would not have this fd used */
					TcpBaseFuncs::CloseFd(&pConnInfo->epollConnFd);
				socketConnMap.erase(pConnInfo->connFd);
				delete pConnInfo;
				socketMapItr = pIpConnInfo->connInfoMap.erase(socketMapItr);
				numRemoved++;
			}

			/* remove host */
			delete pIpConnInfo;
			ipSocketMap.erase(ipAddress);
		}

		return numRemoved;
	}

	void MessageForwarder::IncreaseEventsCount(int numEvents)
	{
		int totalSize = CheckNumConnections() + numEvents;

		if (this->numEvents < totalSize)
		{
			std::unique_lock lock(m_events);
			
			this->numEvents = totalSize;
		}
	}

	void MessageForwarder::IncreaseEvents(struct epoll_event** pEvents,int* numEvents)
	{
		if (*numEvents < this->numEvents)
		{
			/* first connection */
			if (*pEvents == nullptr)
				*pEvents = (struct epoll_event*) calloc(this->numEvents, sizeof(struct epoll_event));
			else
			{
				*pEvents = (struct epoll_event *)realloc(*pEvents, sizeof(struct epoll_event) * this->numEvents);
				memset(*pEvents, 0, sizeof(struct epoll_event) * this->numEvents);
			}
			*numEvents = this->numEvents;
		}
	}

	void MessageForwarder::MarkConnectionsForRemoval(char* ipAddress)
	{
		IpSocketMapItr ipSocketMapItr;
		SocketConnMapItr socketMapItr;
		CONNECTION_INFO* pConnInfo = nullptr;
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		std::shared_lock lockMaps(m_Maps);

		ipSocketMapItr = ipSocketMap.find(ipAddress);

		if (ipSocketMapItr != ipSocketMap.end())
		{
			pIpConnInfo = (*ipSocketMapItr).second;
			
			/* scroll through and mark */
			for (socketMapItr = pIpConnInfo->connInfoMap.begin(); socketMapItr != pIpConnInfo->connInfoMap.end(); socketMapItr++)
			{
				pConnInfo = (*socketMapItr).second;
				pConnInfo->markedForRemoval = true;
			}
		}
		else
			printf("Info: MarkConnectionsForRemoval ip %s not found on ipSocketMap \n", ipAddress);
	}

	int MessageForwarder::PopulateUdpPayload(UDP_MULTICAST_PAYLOAD* pPayload)
	{
		int cc = 0;
		std::string address = "";
		cc = GetHostIp(&address);

		if (cc == 0)
		{
			if (shutdown)
				pPayload->command = (uint16_t)remove;
			else
				pPayload->command = (uint16_t)add;

			strncpy(pPayload->ip_node, address.data(), IP_LENGTH);
			strncpy(pPayload->queue_name, queue_name.data(), QUEUE_NAME_LENGTH);
		}

		return cc;
	}

	int MessageForwarder::ProcessUdpPayload(unsigned char* buffer, uint64_t length)
	{
		int cc = 0;

		/* if the length is less than the smallest version there is a problem */
		if (length < sizeof(UDP_MULTICAST_PAYLOAD_V01))
		{
			printf("Error: ProcessUdpPayload malformed payload: %s \n", GetHexString((char*)buffer,length).c_str());
			cc = malformed_udp_payload;
		}
		
		if (cc == 0)
		{
			UDP_MULTICAST_PAYLOAD* pUdpPayload = (UDP_MULTICAST_PAYLOAD*)buffer;
			if (strncmp(pUdpPayload->queue_name, queue_name.data(), QUEUE_NAME_LENGTH) == 0)
			{
				if (pUdpPayload->command == udp_commands::add)
				{
					cc = AddConnections(pUdpPayload->ip_node);
					if (cc != 0 && cc != connections_already_exist)
						printf("Error: ProcessUdpPayload error creating connections : %d \n", cc);

				}
				else if (pUdpPayload->command == udp_commands::remove)
				{
					if (CheckNumConnectionsIp(pUdpPayload->ip_node) > 0)
					{
						MarkConnectionsForRemoval(pUdpPayload->ip_node);
						cc = WaitForOutStandingMessages(5000, pUdpPayload->ip_node);
						/* If we time out then wait for the next command, if they queue up and fail its ok */
						if (cc == 0)
						{
							int numRemoved = RemoveConnections(pUdpPayload->ip_node,true);
							if (numRemoved == 0)
								printf("Info: ProcessUdpPayload nothing to delete for : %s \n", pUdpPayload->ip_node);
						}
					}
					
				}
				else
				{
					printf("Error: ProcessUdpPayload invalid command: %d : %s \n", pUdpPayload->command, GetHexString((char*)buffer, length).c_str());
					cc = malformed_udp_payload;
				}
			}
			else
			{
				printf("Error: ProcessUdpPayload invalid queue name: %s : %s \n", pUdpPayload->queue_name, GetHexString((char*)buffer, length).c_str());
				cc = invalid_queue_name;
			}

		}
		

		return cc;
	}

	void MessageForwarder::RemoveDataFromReadBuffer(CONNECTION_INFO* pConnInfo, uint32_t length)
	{
		if (length >= pConnInfo->readBufferDataLength)
		{
			pConnInfo->readBufferDataLength = 0;
			memset(pConnInfo->readBuffer, 0, TCP_RAW_READ_BUFFER_SIZE);
		}
		else if(length != 0)
		{
			/* What we are doing here is just overwriting the data and changing the length */
			uint32_t remaining = pConnInfo->readBufferDataLength - length;

			memmove(pConnInfo->readBuffer, &pConnInfo->readBuffer[length], remaining);

			pConnInfo->readBufferDataLength = remaining;
		}

		/* since we use this for lengthToSkip set the value correctly after moving data,
		    but it can be used just to delete data not just skipping header data */
		if (pConnInfo->lengthToSkip > 0)
		{
			if (length >= pConnInfo->lengthToSkip)
				pConnInfo->lengthToSkip = 0;
			else
				pConnInfo->lengthToSkip -= length;
		}

	}

	unsigned char* MessageForwarder::GetReadBufferPos(CONNECTION_INFO* pConnInfo)
	{
		/* get offset into buffer */
		return (unsigned char*) &pConnInfo->readBuffer[pConnInfo->readBufferDataLength];
	}

	uint64_t MessageForwarder::GetReadBufferFreeLength(CONNECTION_INFO* pConnInfo)
	{
		/* get how much more we can fit */
		return TCP_RAW_READ_BUFFER_SIZE - pConnInfo->readBufferDataLength;
	}

	int MessageForwarder::ProcessTcpCommand(CONNECTION_INFO* pConnInfo, TCP_MESSAGE* pTcpMessage)
	{
		int cc = 0;

		if (pTcpMessage->connect_ack_message == 'Y')
			pConnInfo->readyToProcess = true;

		return cc;
	}

	int MessageForwarder::ProcessTcpMessage(CONNECTION_INFO* pConnInfo)
	{
		int cc = 0;

		/* we havent read this tcp payload at all */
		if (pConnInfo->lengthToRead == 0)
		{
			TCP_MESSAGE* pTcpMessage = (TCP_MESSAGE*)pConnInfo->readBuffer;
			pConnInfo->lengthToRead = pTcpMessage->data_length;
			pConnInfo->lengthLeftToRead = pTcpMessage->data_length;
			pConnInfo->lengthToSkip = pTcpMessage->header.fields_length;
			RemoveDataFromReadBuffer(pConnInfo, pConnInfo->lengthToSkip);
		}

		/* now lets read in the buffer */
		if (pConnInfo->lengthToRead > 0 && pConnInfo->readBufferDataLength > 0) /* saftey check if we didnt just skip over everything we read in */
		{
			uint32_t dataToWrite = 0;

			/* our buffer is bigger than what we need */
			if (pConnInfo->readBufferDataLength > pConnInfo->lengthLeftToRead)
				dataToWrite = pConnInfo->lengthLeftToRead;
			/* our buffer is smaller than what we need */
			else if(pConnInfo->readBufferDataLength < pConnInfo->lengthLeftToRead)
				dataToWrite = pConnInfo->readBufferDataLength;
			/* equal */
			else
				dataToWrite = pConnInfo->lengthLeftToRead;

			pConnInfo->ss.append((char*)pConnInfo->readBuffer, dataToWrite);
			pConnInfo->lengthLeftToRead -= dataToWrite; /* will be set to 0 once entire message has been consumed */

			/* move buffer correctly */
			RemoveDataFromReadBuffer(pConnInfo, dataToWrite);

			if (pConnInfo->ss.length() >= pConnInfo->lengthToRead) /* do we have everything */
			{
				MESSAGE msg;
				/* since we are locking the map and no error on socket we will have an entry */
				IP_CONNECTION_INFO* pIpConnInfo = (*ipSocketMap.find(pConnInfo->peerIpAddress)).second;
				/* clear out our variables */
				pConnInfo->lengthToSkip = 0;
				pConnInfo->lengthToRead = 0;
				
				msg.isResponse = true;
				msg.ss.assign((char*)pConnInfo->ss.data(), pConnInfo->ss.length());
				msg.replyIpAddress = pConnInfo->peerIpAddress;
				cc = pQueueManager->PutDataOnQueue<MESSAGE>(&i_input_queue_name, msg);

				if (cc != QueueWrapper::QUEUE_SUCCESS)
				{
					printf("Error: ProcessTcpMessage (PutDataOnQueue) error code : %d dumping hex \n %s \n", cc,
						GetHexString((char*)pConnInfo->ss.data(), pConnInfo->ss.length()).c_str());
				}
					

				pConnInfo->ss.clear();

				if (manageOutStanding == true && iMode == client) /* only clients get responses decrement */
					DecrementOutStanding(pIpConnInfo);
				else if (manageOutStanding == true && iMode == server) /* only servers get requests increment */
					IncrementOutStanding(pIpConnInfo);
			}

			
		}

		return cc;
	}

	int MessageForwarder::ProcessTcpPayload(CONNECTION_INFO* pConnInfo)
	{
		int cc = 0;

		RemoveDataFromReadBuffer(pConnInfo, pConnInfo->lengthToSkip); /* do we have anything we need to skip */

		if (pConnInfo->lengthToRead == 0 && pConnInfo->readBufferDataLength >= sizeof(TCP_MESSAGE_V01)) /* we use V01 because any other data we are not ready for will be skipped */
		{
			TCP_MESSAGE* pTcpMessage = (TCP_MESSAGE*)pConnInfo->readBuffer;
			if (pTcpMessage->data_length == 0)/* no incoming payload it is a tcp command */
			{
				cc = ProcessTcpCommand(pConnInfo, pTcpMessage);
				if (cc != 0)
					printf("Error: ProcessTcpPayload (ProcessTcpCommand) error code is : %d \n", cc);
				pConnInfo->lengthToSkip = pTcpMessage->header.fields_length;
				RemoveDataFromReadBuffer(pConnInfo, pConnInfo->lengthToSkip);
			}
			else
				cc = ProcessTcpMessage(pConnInfo);
		}
		/* if we are not skipping and read in the header (at least ) */
		else if(pConnInfo->lengthToSkip == 0 && pConnInfo->lengthToRead > 0) /* continuation of message */
			cc = ProcessTcpMessage(pConnInfo);

		/* Ok so why are we doing this, in the event we read in the last of a message + the next message either just header or 
		   header + data we want to process it */
		if (pConnInfo->readBufferDataLength >= sizeof(TCP_MESSAGE_V01))
			cc = ProcessTcpPayload(pConnInfo);

			

		return cc;
	}

	void MessageForwarder::UdpThread(bool* running, bool* stopped, void* usrPtr)
	{
		MessageForwarder* pMessageForwarder = (MessageForwarder *)usrPtr;
		int sendInterval = SERVER_UDP_INTERVAL;
		UDP_MULTICAST_PAYLOAD payload = { 0 };
		unsigned char buffer[UDP_RAW_BUFFER_SIZE] = { 0 };
		uint64_t bufferLength = sizeof(buffer);
		std::string ss = "";
		struct epoll_event events = { 0 };
		int numEvents = 0;
		int cc = 0;

		while (*running == true)
		{
			if (pMessageForwarder->iMode == client)//if we are in client mode lets get statuses from servers
			{
				numEvents = epoll_wait(pMessageForwarder->epollUdpFd, &events, 1, pMessageForwarder->internalThreadTimeoutMilli);

				if (numEvents > 0)
				{
					if ((events.events & EPOLLIN) && !(events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)))// we have some data
					{
						uint64_t dataRead = 0;

						memset(buffer, 0, UDP_RAW_BUFFER_SIZE);
						cc = pMessageForwarder->RecvFrom(pMessageForwarder->udpFd, buffer, bufferLength, &dataRead);

						if (cc == 0 && dataRead > 0)
						{
							ss.assign((char*)buffer, dataRead);
							cc = pMessageForwarder->pQueueManager->PutDataOnQueue<std::string>(&pMessageForwarder->i_command_queue_name, ss);

							if (cc != QueueWrapper::QUEUE_SUCCESS)
								printf("Error: UdpThread (PutDataOnQueue) error code %d \n", cc);
						}
						else
						{
							printf("Info: UdpThread (RecvFrom) error code is %d : data read is %lld \n", cc, dataRead);
							cc = pMessageForwarder->UdpBaseFuncs::GetSocketError(pMessageForwarder->udpFd);
							if (cc != 0)
							{
								printf("Error: UdpThread (RecvFrom) socket error code %d : %s \n", cc, strerror(cc));
								cc = pMessageForwarder->ReCreateUdpServer();
							}
						}
					}

					if (events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
					{
						cc = pMessageForwarder->ReCreateUdpServer();

						if (cc != 0)
							printf("Error: UdpThread (numEvents > 0) error code %d : %s \n", cc, strerror(cc));
					}
				}
				else if (numEvents == 0)
				{
					printf("UdpThread: Udp Timeout... \n");
					if (pMessageForwarder->udpFd == 0)
					{
						cc = pMessageForwarder->ReCreateUdpServer();

						if (cc != 0)
							printf("Error: UdpThread (numEvents == 0) error code %d : %s \n", cc, strerror(cc));
					}
				}
				else if (errno == EINTR)
					usleep(1000);// system interrupt just continue
				else
				{
					printf("Error: UdpThread (numEvents < 0) error code %d : %s \n", errno, strerror(errno));

					cc = pMessageForwarder->ReCreateUdpServer();

					if (cc != 0)
						printf("Error: UdpThread ReCreateUdpServer (numEvents < 0) error code %d : %s \n", cc, strerror(cc));

					usleep(sendInterval);
				}
			}
			else if (pMessageForwarder->iMode == server)//if we are in server mode lets clients know of our existence 
			{
				cc = pMessageForwarder->PopulateUdpPayload(&payload);

				if(cc != 0)
					printf("Error: UdpThread (PopulateUdpPayload) error code %d : %s \n", cc, strerror(cc));

				if (cc == 0)
				{
					cc = pMessageForwarder->SendToMultiCast((char*)pMessageForwarder->multiCastGroup.c_str(), pMessageForwarder->udpFd,
						                                    pMessageForwarder->port, (unsigned char*)&payload, sizeof(UDP_MULTICAST_PAYLOAD));

					if (cc != 0 && cc != ENOBUFS)
					{
						printf("Error: UdpThread (SendToMultiCast) error code %d : %s \n", cc, strerror(cc));
						cc = pMessageForwarder->ReCreateUdpClient();
					}
					else if (cc == ENOBUFS)
						printf("Info: UdpThread (SendToMultiCast) buffer full \n");
						
				}

				usleep(sendInterval);// (sleep for roughly 1 second) this time doesnt need to be exact 
			}
			else // should not happen 
				usleep(sendInterval);
		}

		*stopped = true;
	}

	void MessageForwarder::UdpCommandProcessThread(bool* running, bool* stopped, void* usrPtr)
	{
		MessageForwarder* pMessageForwarder = (MessageForwarder *)usrPtr;
		int cc = 0;
		RegisterQueueReadThread<std::string>(pMessageForwarder->i_command_queue_name);
		std::string data = "";

		while (*running == true)
		{
			data = pMessageForwarder->pQueueManager->GetDataFromQueue<std::string>(&pMessageForwarder->i_command_queue_name,
				                                                                    pMessageForwarder->internalThreadTimeoutMilli, &cc);
			if (cc == QueueWrapper::QUEUE_SUCCESS)
			{
				cc = pMessageForwarder->ProcessUdpPayload((unsigned char*)data.data(), data.length());

				if (cc != 0 && cc != connections_already_exist)
					printf("Error: UdpCommandProcessThread (ProcessUdpPayload) error code %d \n", cc);
			}
			else if(cc != QueueWrapper::QUEUE_TIMEOUT)
				printf("Error: UdpCommandProcessThread (GetDataFromQueue) error code %d \n", cc);

			SleepOnQueue(cc);
		}
		RemoveQueueReadThread<std::string>(pMessageForwarder->i_command_queue_name);
		*stopped = true;
	}

	void MessageForwarder::MessageReaderThread(bool* running, bool* stopped, void* usrPtr)
	{
		MessageForwarder* pMessageForwarder = (MessageForwarder *)usrPtr;
		int cc = 0;
		int numEvents = 1;
		int numReady = 0;
		int i = 0;
		uint64_t numBytesRead = 0;
		struct epoll_event* pEvents = (struct epoll_event*) calloc(numEvents, sizeof(struct epoll_event));
		SocketConnMapItr socketMapItr;
		CONNECTION_INFO* pConnInfo = nullptr;
		std::shared_lock lock(pMessageForwarder->m_events);
		std::shared_lock lockMap(pMessageForwarder->m_Maps); /* lock the maps since we need the pointers */
		lock.unlock(); /* onstart just unlock seems there is no defer lock */
		lockMap.unlock(); /* onstart just unlock seems there is no defer lock */
		
		while (*running == true)
		{
			/* get the shared lock and increase before wait if necessary */
			lock.lock();
			pMessageForwarder->IncreaseEvents(&pEvents, &numEvents);
			lock.unlock();

			/* if in the event we do not have enough events (for only 1 epoll wait) it will come on the next one */
			numReady = epoll_wait(pMessageForwarder->connReadEpollFd, pEvents, numEvents, pMessageForwarder->internalThreadTimeoutMilli);

			/* now lets process any reads */

			if (numReady > 0)
			{
				for (i = 0; i < numReady; i++)
				{
					if ((pEvents[i].events & EPOLLIN) && !(pEvents[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)))//we have data to read
					{
						lockMap.lock();
						socketMapItr = pMessageForwarder->socketConnMap.find(pEvents[i].data.fd);

						if (socketMapItr != pMessageForwarder->socketConnMap.end())
						{
							pConnInfo = (*socketMapItr).second;
							
							do
							{
								cc = pMessageForwarder->Recv(pEvents[i].data.fd, pMessageForwarder->GetReadBufferPos(pConnInfo),
									pMessageForwarder->GetReadBufferFreeLength(pConnInfo), &numBytesRead);
								if (cc == 0 && numBytesRead > 0)
								{
									pConnInfo->readBufferDataLength += numBytesRead;
									cc = pMessageForwarder->ProcessTcpPayload(pConnInfo);

									if (cc != 0)
										printf("Error: MessageReaderThread (ProcessTcpPayload) error code : %d \n", cc);
								}
								else
								{
									if (numBytesRead == 0 && cc == 0 && pConnInfo->readBufferDataLength == TCP_RAW_READ_BUFFER_SIZE)
										printf("Error: No more free buffer space on %d dumping hex \n%s\n",
											pEvents[i].data.fd, GetHexString(pConnInfo->readBuffer, pConnInfo->readBufferDataLength).c_str());
									pMessageForwarder->RemoveDataFromReadBuffer(pConnInfo, TCP_RAW_READ_BUFFER_SIZE); /* clear the buffer */
								}
							} while ((cc == 0 && numBytesRead > 0) || cc == EINTR); /* if we interrupt try again or waiting on data to be sent over */
							
						}
						else
							printf("Info: Unknown Socket : %d \n", pEvents[i].data.fd); /* possibly get here on a forced connection cut */

						/* we have finished processing we need to re arm the fd */
						pMessageForwarder->ArmReadConnFd(pEvents[i].data.fd, false);
						lockMap.unlock();
					}
					else if (pEvents[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) /* connection was closed */
					{
						pMessageForwarder->TcpBaseFuncs::DeleteSocketFromEpoll(pEvents[i].data.fd, pMessageForwarder->connReadEpollFd);
						pMessageForwarder->RemoveConnection(pEvents[i].data.fd);
					}
				}
			}
			else if (numReady != 0)
			{
				cc = errno;
				if (cc != EINTR)//system interrupt just continue
				{
					printf("Error MessageReaderThread (epoll_wait) %s error code : %d  \n", strerror(cc), cc);
					/* maybe some alerting */
				}
				usleep(1000);
			}
		}
		free(pEvents);
		*stopped = true;
	}

	void MessageForwarder::MessageWriterThread(bool* running, bool* stopped, void* usrPtr)
	{
		MessageForwarder* pMessageForwarder = (MessageForwarder *)usrPtr;
		int cc = 0;
		RegisterQueueReadThread<MESSAGE>(pMessageForwarder->i_output_queue_name);
		std::shared_lock lockMap(pMessageForwarder->m_Maps); /* lock the maps since we need the pointers */
		lockMap.unlock();
		MESSAGE msg;
		uint32_t startingOffset = 0;
		uint32_t currentOffset = 0;
		SocketConnMapItr socketConnMapItr;
		IpSocketMapItr ipSocketMapItr;
		TCP_MESSAGE tcp_message; /* initalizers set in header */
		IP_CONNECTION_INFO* pIpConnInfo = nullptr;
		CONNECTION_INFO* pConnInfo = nullptr;


		while (*running == true)
		{
			msg = pMessageForwarder->pQueueManager->GetDataFromQueue<MESSAGE>(&pMessageForwarder->i_output_queue_name,
				                                                              pMessageForwarder->internalThreadTimeoutMilli, &cc);
			if (cc == QUEUE_SUCCESS)
			{
				lockMap.lock();

				if (pMessageForwarder->socketConnMap.size() == 0) /* no connections */
				{
					msg.reasonCode = no_connection;
					msg.delivered = false;
					pMessageForwarder->ExtractUsrData(&msg);
					cc = pMessageForwarder->pQueueManager->PutDataOnQueue<MESSAGE>(&pMessageForwarder->i_input_queue_name, msg);

					if (cc != QUEUE_SUCCESS)
					{
						printf("Error MessageWriterThread (PutDataOnQueue) %d \n", cc);
						cc = QUEUE_SUCCESS; /* set to 0 so we dont sleep on the bottom */
					}
						
				}
				else
				{
					if (msg.replyIpAddress.length() != 0)
					{
						ipSocketMapItr = pMessageForwarder->ipSocketMap.find(msg.replyIpAddress);

						if (ipSocketMapItr != pMessageForwarder->ipSocketMap.end())
						{
							pIpConnInfo = (*ipSocketMapItr).second;
							startingOffset = pMessageForwarder->GetNextConnectionOffsetIp(pIpConnInfo); /* lets get our starting position */
							currentOffset = startingOffset;
							socketConnMapItr = pIpConnInfo->connInfoMap.begin();
						}
						else
						{
							msg.reasonCode = no_reply_ip;
							cc = no_reply_ip; /* force to send reply back */
						}
					}
					else
					{
						startingOffset = pMessageForwarder->GetNextConnectionOffset(); /* lets get our starting position */
						currentOffset = startingOffset;
						socketConnMapItr = pMessageForwarder->socketConnMap.begin();
					}

					if (cc == 0) /* ok lets send */
					{
						uint64_t bufferLength = msg.ss.length();
						uint64_t lengthSent = 0;
						uint64_t bufferOffset = 0;
						bool sent = false;
						std::advance(socketConnMapItr, currentOffset);

						/* in this do while we will only exit on error if the socket has been closed / cut */
						do
						{
							pConnInfo = (*socketConnMapItr).second;

							if (pConnInfo->markedForRemoval == false && pConnInfo->readyToProcess == true)
							{
								std::lock_guard<std::mutex> lock(pConnInfo->m_conn); /* make sure no other sending thread is currently calling send */

								do
								{
									cc = pMessageForwarder->Send(pConnInfo->connFd, (unsigned char*)&msg.ss[bufferOffset], bufferLength - bufferOffset, &lengthSent);
									bufferOffset += lengthSent;

									if (bufferOffset != bufferLength && cc == ENOBUFS  ) /* normally we shouldnt get here but if we do */
									{
										usleep(1000); /* quickly sleep so we can try again */
									}
										

								} while (bufferOffset != bufferLength && (cc == ENOBUFS || cc == 0 || cc == EINTR));

								if (bufferOffset == bufferLength && cc == 0)
									sent = true;
							}
							else /* socket unusable go to next one */
							{
								currentOffset++;
								socketConnMapItr++;
								if (msg.replyIpAddress.length() != 0) /* get socket map from ip Conn Info we have a reply ip */
								{
									if (socketConnMapItr == pIpConnInfo->connInfoMap.end())
									{
										currentOffset = 0;
										socketConnMapItr = pIpConnInfo->connInfoMap.begin();
									}
								}
								else
								{
									if (socketConnMapItr == pMessageForwarder->socketConnMap.end())
									{
										currentOffset = 0;
										socketConnMapItr = pMessageForwarder->socketConnMap.begin();
									}
								}
								
							}

							/* once we loop around once and were not successful exit if the socket is not valid to us go to the next one */
						} while (sent == false && cc == 0 && currentOffset != startingOffset); 


						if (cc != 0 || (sent == false && cc == 0)) /* we had an error sending reply back appropriatly or no sockets were ready */
						{
							pConnInfo->readyToProcess = false; /* set it to false so we do not want to use this socket it, should be closed by recv threads */
							msg.reasonCode = no_connection;
							cc = no_connection; /* force to send message below */
						}
						else /* on success */
						{
							pIpConnInfo = (*pMessageForwarder->ipSocketMap.find(pConnInfo->peerIpAddress)).second;
							if (pMessageForwarder->manageOutStanding == true && pMessageForwarder->iMode == server) /* only servers respond on send decrement  */
								pMessageForwarder->DecrementOutStanding(pIpConnInfo);
							else if (pMessageForwarder->manageOutStanding == true && pMessageForwarder->iMode == client) /* only clients send requests increment */
								pMessageForwarder->IncrementOutStanding(pIpConnInfo);
						}
					}
					
					if(cc != 0)
					{
						msg.delivered = false;
						pMessageForwarder->ExtractUsrData(&msg);
						cc = pMessageForwarder->pQueueManager->PutDataOnQueue<MESSAGE>(&pMessageForwarder->i_input_queue_name, msg);

						if (cc != QUEUE_SUCCESS)
							printf("Error MessageWriterThread (PutDataOnQueue) %d \n", cc);
					}
				}

				lockMap.unlock();
			}
			
			SleepOnQueue(cc);
		}
		RemoveQueueReadThread<MESSAGE>(pMessageForwarder->i_output_queue_name);
		*stopped = true;
	}

	void MessageForwarder::MessageListenerThread(bool* running, bool* stopped, void* usrPtr)
	{
		MessageForwarder* pMessageForwarder = (MessageForwarder *)usrPtr;
		int cc = 0;
		TCP_MESSAGE tcp_message; /* initalizers set in header */
		std::string peerIpdAddress = "";
		uint64_t sizeSent = 0;
		int connFd = 0;
		bool timeout = false;

		while (*running == true)
		{
			cc = pMessageForwarder->Accept(pMessageForwarder->listenSocketFd, &connFd, pMessageForwarder->listenSocketEpollFd,
				                           pMessageForwarder->internalThreadTimeoutMilli, &peerIpdAddress, &timeout, true);

			if (cc == 0 && timeout == false)
			{
				pMessageForwarder->IncreaseEventsCount(1);

				cc = pMessageForwarder->AddConnection((char*)peerIpdAddress.data(), connFd);

				if (cc == 0)
				{
					cc = pMessageForwarder->ArmReadConnFd(connFd, true);

					if (cc != 0)
						printf("Error: MessageListenerThread (ArmReadConnFd) error code : %d \n", cc);
				}

				if (cc == 0)
				{
					cc = pMessageForwarder->TcpBaseFuncs::SetIOBufferSize(connFd, pMessageForwarder->ioBufferSize, pMessageForwarder->ioBufferSize);
					if (cc != 0)
						printf("Error: MessageListenerThread (SetIOBufferSize) error code : %d \n", cc);
				}

				if (cc == 0)
				{
					tcp_message.connect_ack_message = 'Y';
					tcp_message.header.fields_length = sizeof(TCP_MESSAGE);
					tcp_message.data_length = 0; /* length of 0 means its a command */

					cc = pMessageForwarder->Send(connFd, (unsigned char *)&tcp_message, sizeof(TCP_MESSAGE), &sizeSent);

					if (cc != 0)
						printf("Error: MessageListenerThread Failed to send connect ack error code : %d..\n", cc);
				}

				if (cc != 0)
				{
					printf("Error: MessageListenerThread Failed to set up connection socket for error %d \n", cc);
					pMessageForwarder->TcpBaseFuncs::DeleteSocketFromEpoll(connFd, pMessageForwarder->connReadEpollFd);
					pMessageForwarder->RemoveConnection(connFd);
				}

			}
			else if (cc == EINTR)
				usleep(1000); //system interrupt just continue
			else if(cc != 0)
			{
				printf("Error: MessageListenerThread (Accept) error code : %d.. Recreating listener socket \n", cc);
				cc = pMessageForwarder->ReCreateTcpServer();

				if(cc != 0)
					printf("Error: MessageListenerThread (ReCreateTcpServer) error code : %d \n", cc);

				usleep(1000);// sleep for 1 millisecond just incase we get stuck in a tight loop for failing to create a socket 
			}
		}
		*stopped = true;
	}
};

