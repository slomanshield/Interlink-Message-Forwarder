#ifndef MESSAGE_FORWARDER_DEFINES_H
#define MESSAGE_FORWARDER_DEFINES_H

#include "TcpBaseFuncs.h"
#include "UdpBaseFuncs.h"

/* The concept for these structures are there are no version data 
   there could be a case where clients could be upgrades before servers
   and vice versa. So data should always be appended. When it comes to udp its easy you just 
   wont see the data. For tcp we must skip over it (header length) */
namespace MessageForwarder
{
	/* 2^19 */
	#define TCP_RAW_READ_BUFFER_SIZE 524288
	/* 2^17 */
	#define UDP_RAW_BUFFER_SIZE 131072

	#define MULTI_CAST_GROUP "226.20.1.1"

	#define IP_LENGTH 15
	#define QUEUE_NAME_LENGTH 256

	#define MAX_COMMAND_QUEUE_OUTSTANDING 100

	/* send a pulse (roughy) every 1 second */
	#define SERVER_UDP_INTERVAL 1000000 

	enum udp_commands { add = 1, remove = 2 };

	enum error_codes 
	{ 
		success = 0, 
		no_connection = 1,
		buffers_full = 2,
		no_reply_ip = 3,
		bad_reply_socket = 4,
		connections_not_closed = 5,
		failed_starting_processing_threads = 6,
		failed_starting_listening_thread = 7,
		failed_starting_udp_command_thread = 8,
		failed_starting_udp_thread = 9,
		connections_exist_on_shutdown = 10,
		failed_stopping_udp_command_thread = 11,
		failed_stopping_udp_thread = 12,
		failed_stopping_listening_thread = 13,
		failed_stopping_processing_threads = 14,
		udp_socket_exists = 15,
		tcp_server_socket_exists = 16
	};

	enum mode {client = 1, server = 2 };

	enum payload_udp_error 
	{
		malformed_udp_payload = 1,
		connections_already_exist = 2, 
		invalid_queue_name = 3
	};

	enum payload_tcp_error
	{
		malformed_tcp_payload = 1,
		connection_already_exists = 2
	};

	struct TCP_MESSAGE_HEADER
	{
		/* this is done so when new fields are added programs that are not updated yet can still skip over */
		uint32_t fields_length = 0; /* length of the fields ahead */
	};

	/* Version 01 TCP */
	struct TCP_MESSAGE_V01
	{
		TCP_MESSAGE_HEADER header; /* length of the fields ahead */
		uint32_t data_length = 0; /* little endian binary */
		char connect_ack_message = 0; /* ascii Y/N or \0, to be used on recv marking the connection ready */
		char filler[3] = { 0 }; /* align for 4 byte boundary since max is uint32 */
	};

	/* Version 01 UDP */
	struct UDP_MULTICAST_PAYLOAD_V01
	{
		char ip_node[IP_LENGTH + FOR_NULL] = { 0 }; /* ascii */
		char queue_name[QUEUE_NAME_LENGTH + FOR_NULL] = { 0 }; /* ascii */
		char filler = 0; /* align for 2 byte boundary since max is short */
		uint16_t command = 0; /* little endian binary */
	};

	typedef UDP_MULTICAST_PAYLOAD_V01 UDP_MULTICAST_PAYLOAD; /* Current Version */
	typedef TCP_MESSAGE_V01 TCP_MESSAGE; /* Current Version */

	/* Message used to send to MessageForwarder Queues */
	struct MESSAGE
	{
		std::string ss = "";
		bool delivered = true; /* tells the application if the message was sent or not, init to true */
		bool isResponse = false; /* tells application is is this response */
		error_codes reasonCode = error_codes::success;
		std::string replyIpAddress = ""; /* used on send, sent back on recieve (IPV4), if is not sent message will fail */
	};

	struct CONNECTION_INFO
	{
		int connFd = 0; /* used by both */
		int epollConnFd = 0; /* epollfd to be used to check if connection is up used by clients */
		struct epoll_event eventConn = { 0 }; /* event used to check connection status */
		std::string peerIpAddress = "";
		bool markedForRemoval = false; /* used mainly when a server side is going down so the senders put no data on */
		bool readyToProcess = false; /* to be used for readers only when an ack is sent by the server */
		std::mutex m_conn; /* mutex used to lock when sending data (only 1 thread can send at a time) */
		std::string ss = ""; /* used by readers only */
		uint64_t lengthToRead = 0; /* used by readers only */
		uint64_t lengthLeftToRead = 0; /* used by readers only */
		uint32_t lengthToSkip = 0; /* used by readers only */
		char readBuffer[TCP_RAW_READ_BUFFER_SIZE] = { 0 };
		uint64_t readBufferDataLength = 0; /* used by readers only current length of buffer */
	};
	typedef std::unordered_map<int, CONNECTION_INFO*> SocketConnMap; /* used by sending/reading threads (fd, connection) */

    struct IP_CONNECTION_INFO
	{
		SocketConnMap connInfoMap;
		/* used by sender threads (server mode) */
		uint32_t ipMapConnOffset = 0; /* used to round robin between recv threads */
		uint64_t numOutStanding = 0; /* number of messages that have been sent used mainly when removing the connection, can be ignored */
		std::shared_mutex m_outStanding; /* mutex used for updating the value */
		std::mutex    m_ipMapConnOffset;
	};

	typedef std::unordered_map<std::string, IP_CONNECTION_INFO*> IpSocketMap; /* used by manage threads / reply(different sender mode) threads (peer ip,connection IP_CONNECTION_INFO) */
	typedef typename std::unordered_map<int, CONNECTION_INFO*>::const_iterator SocketConnMapItr;
	typedef typename std::unordered_map<std::string, IP_CONNECTION_INFO*>::const_iterator IpSocketMapItr;

	typedef std::list< CONNECTION_INFO*> ConnInfoList;
	typedef typename std::list< CONNECTION_INFO*>::const_iterator ConnInfoListItr;

};


#endif