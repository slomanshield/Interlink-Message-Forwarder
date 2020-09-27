#ifndef MESSAGE_FORWARDER_H
#define MESSAGE_FORWARDER_H

#include "MessageForwarderDefines.h"
/* The application can use this in 2 modes one way or 2 way 
   If its 2 way you want manageOutStanding to be true if its one way (meaning server applicaiton will not reply)
   then you set that to off so the num messages per connection are not managed */
namespace MessageForwarder
{
	class MessageForwarder : public TcpBaseFuncs , public UdpBaseFuncs
	{
		public:
			MessageForwarder(uint16_t port, std::string queue_name, std::string input_queue_name, std::string output_queue_name,
							 int numSenders, int numReaders, int numConnectionsPerHost, mode iMode, int connectionTimeoutMilli,
				             uint64_t ioBufferSize, bool manageOutStanding = true, std::string multiCastGroup = "");
			~MessageForwarder();
			int Init();
			int Start(std::string* tcpListenIP = nullptr);
			int Stop(int timeoutMilli, bool waitForOutStanding = true, bool forceServerClose = false);
			void BeginStop(); /* Server only, used by a server to let clients know to stop sending messages and disconnect */
			int SendData(char* s,uint32_t length, std::string* replyIp, MESSAGE* pMsgIn);
			int WaitForConnections(int timeoutMilli);
		private:
			int InitUdp();
			int InitUdpServer();
			int InitTcpServer();
			void DestroyUdp();
			void DestroyTcpServer();
			int StartUdp();
			int StartTcpServer();
			int StartProcessingThreads();
			int StopUdp();
			int StopTcpServer();
			int StopProcessingThreads();
			int InitConnectInfo(CONNECTION_INFO* pConnInfo,char* ipAddress);
			int PopulateUdpPayload(UDP_MULTICAST_PAYLOAD* pPayload);
			int ProcessUdpPayload(unsigned char* buffer,uint64_t length);
			int ProcessTcpPayload(CONNECTION_INFO* pConnInfo);
			int ReCreateUdpServer();
			int ReCreateUdpClient();
			int ReCreateTcpServer();

			int AddConnections(char* ipAddress); /* used when add request comes from server to client */
			int AddConnection(char* ipAddress,int connFd); /* used when server gets connection from a client */
			int RemoveConnections(char* ipAddress,bool removeHostEntry); /* used when shutdown request comes from server */
			int RemoveConnections(); /* used when stop is called */
			void RemoveConnection(int connFd); /* used when connection is cut and reader reacts */
			void MarkConnectionsForRemoval(char* ipAddress); /* used when we get a disconnect request from a server */
			int CheckNumConnectionsIp(char* ipAddress);
			int CheckNumConnections();
			int WaitForTcpConnect(ConnInfoList* pConnInfoList);
			int WaitForOutStandingMessages(int timeoutMilli,char* ipAddress);
			int WaitForOutStandingMessages(int timeoutMilli);
			int AddSocketsToReadFd(ConnInfoList* pConnInfoList);
			void AddNewConnections(char* ipAddress, ConnInfoList* pConnInfoList);
			int ProcessTcpMessage(CONNECTION_INFO* pConnInfo);
			int ProcessTcpCommand(CONNECTION_INFO* pConnInfo, TCP_MESSAGE* pTcpMessage);
			void RemoveDataFromReadBuffer(CONNECTION_INFO* pConnInfo, uint32_t length);
			unsigned char* GetReadBufferPos(CONNECTION_INFO* pConnInfo);
			uint64_t GetReadBufferFreeLength(CONNECTION_INFO* pConnInfo);
			void IncreaseEventsCount(int numEvents);
			void IncreaseEvents(struct epoll_event** pEvents, int* numEvents);
			void IncrementOutStanding(IP_CONNECTION_INFO* pIpConnInfo);
			void DecrementOutStanding(IP_CONNECTION_INFO* pIpConnInfo);
			uint32_t GetNextConnectionOffset();
			uint32_t GetNextConnectionOffsetIp(IP_CONNECTION_INFO* ipConnInfo);
			int ArmReadConnFd(int sockFd, bool add);
			void ExtractUsrData(MESSAGE* pMsg);
		private:

			/* used by sender threads (client mode)*/
			uint32_t fdMapConnOffset;
			std::mutex    m_fdMapConnOffset;
			SocketConnMap socketConnMap;

			/* used by sender threads (server mode )
			   and management threads */
			IpSocketMap ipSocketMap;

			/* used by all threads */
			std::shared_mutex m_Maps;

			/* used by reader and listener */
			std::shared_mutex m_events;
			int numEvents;

			/* socket fds */
			int udpFd;
			int epollUdpFd;
			int connReadEpollFd;
			int listenSocketFd;
			int listenSocketEpollFd;
			
			/* Class Members */
			uint16_t port;
			int connectionTimeoutMilli;
			bool shutdown;
			bool manageOutStanding;
			std::string queue_name;
			std::string i_command_queue_name;
			std::string i_output_queue_name;
			std::string i_input_queue_name;
			static threadProcess UdpThread;
			static threadProcess UdpCommandProcessThread;
			static threadProcess MessageReaderThread;
			static threadProcess MessageWriterThread;
			static threadProcess MessageListenerThread;
			ThreadWrapper <threadProcess> UdpThreadHandler;
			ThreadWrapper <threadProcess> UdpCommandProcessThreadHandler;
			ThreadWrapper <threadProcess> MessageReaderThreadHandler;
			ThreadWrapper <threadProcess> MessageWriterThreadHandler;
			ThreadWrapper <threadProcess> MessageListenerThreadHandler;
			int numReaders;
			int numSenders;
			int numConnectionsPerHost;
			int internalThreadTimeoutMilli;
			uint64_t ioBufferSize;
			mode iMode;
			std::string multiCastGroup;
			std::string listenOverrideTcpIP;
			QueueManager* pQueueManager;

	};
};

#endif