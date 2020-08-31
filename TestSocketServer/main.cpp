#include "../Global/MessageForwarder.h"

using namespace MessageForwarder;

/* main server */
int main(int argc, char** argv)
{
	int cc = 0;
	TcpBaseFuncs tcpBaseFuncs;
	UdpBaseFuncs udpBaseFuncs;
	int sockFd = 0;
	int connFd = 0;
	int epollListenFd = 0;
	std::string peer_ip = "";
	bool timeout = false;
	struct epoll_event event = { 0 };
	struct epoll_event events = { 0 };
	int epollFd = epoll_create1(0);
	int numEvents = 0;
	uint64_t lengthRead = 0;
	bool udp = false;
	int count = 0;
	std::string inputQueueName = "input_queue";
	std::string outputQueueName = "reply_queue";
	std::string tmp = "";
	QueueManager* pQueueManager = QueueWrapper::QueueManager::Instance();
	pQueueManager->CreateQueue<MESSAGE>(&inputQueueName, nullptr, nullptr);
	pQueueManager->CreateQueue<MESSAGE>(&outputQueueName, nullptr, nullptr);
	RegisterQueueReadThread(inputQueueName);
	MESSAGE snd_msg;
	MESSAGE msg;
	MessageForwarder::MessageForwarder msgFrwder(30001, "TIER3_QUEUE", "input_queue", "reply_queue", 20, 20, 50, server, 5000, 50000000,true);
	msgFrwder.Init();
	msgFrwder.Start();

	/*if (udp != true)
	{
		tcpBaseFuncs.Bind(30000, &sockFd, &epollListenFd, 20, true);
		tcpBaseFuncs.Accept(sockFd, &connFd, epollListenFd, -1, &peer_ip, &timeout, true);//infintie wait with -1
		tcpBaseFuncs.SetIOBufferSize(connFd, 10000000, 10000000);

		event.data.fd = connFd;
		event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
		tcpBaseFuncs.AddSocketToEpoll(connFd, epollFd, &event);

		/* in this case there should be a thread waiting on accept passing sockets through a queue but for now we are just testing
		   so top down */
		/*while (true)
		{
			printf("waiting...\n");
			numEvents = epoll_wait(epollFd, &events, 1, 5000);

			if (numEvents > 0)// we have an event
			{
				/* we do not want to read any data on a socket that was disconnected could have partial data also we would have no one to send back to (probably) */
				/*if ((events.events & EPOLLIN) && !(events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)))//we have data to read
				{
					printf("Data Event Triggered \n");
					char buffer[50000 + FOR_NULL] = { 0 };
					uint64_t bufferLength = sizeof(buffer) - 1;
					uint64_t dataRead = 0;

					do
					{
						cc = tcpBaseFuncs.Recv(events.data.fd, (unsigned char*)&buffer, bufferLength, &dataRead);
						if (cc == 0 && dataRead > 0)
						{
							printf("buffer data is %s size read %lld\n", buffer, dataRead);
							memset(buffer, 0, sizeof(buffer));
						}
					} while (cc == 0 && dataRead > 0);//we read all the data then stop (for epollet)

				}

				if (events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
				{
					printf("Closing %d ...\n", connFd);
					tcpBaseFuncs.CloseFd(&connFd);
				}

			}
			else if (numEvents == 0)
				printf("timeout waiting... \n");
			else
			{
				printf("error exiting %s \n", strerror(errno));
				break;
			}
				
		}
	}
	else // udp
	{
		udpBaseFuncs.Bind("226.20.1.1", 30000, &sockFd, &epollListenFd, true);

		while (true)
		{
			printf("waiting...\n");
			numEvents = epoll_wait(epollListenFd, &events, 1, 5000);

			if (numEvents > 0)// we have an event
			{
				/* we do not want to read any data on a socket that was disconnected could have partial data also we would have no one to send back to (probably) */
				/*if ((events.events & EPOLLIN) && !(events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)))//we have data to read
				{
					printf("Data Event Triggered \n");
					char buffer[100000 + FOR_NULL] = { 0 };
					uint64_t bufferLength = sizeof(buffer);
					int64_t dataRead = 0;
					std::string address = "";

					/* buffer must be big enough for 1 dgram */
					/*cc = udpBaseFuncs.RecvFrom(events.data.fd,(unsigned char*)&buffer, bufferLength, &dataRead);
					if (cc == 0 && dataRead > 0)
					{
						printf("Recived Dgram from %s buffer data is %s \n", address.c_str(),buffer);
						memset(buffer, 0, sizeof(buffer));
					}
					

				}

				if (events.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
				{
					printf("Closing %d ...\n", connFd);
					udpBaseFuncs.CloseFd(&connFd);
				}

			}
			else if (numEvents == 0)
				printf("timeout waiting... \n");
			else
			{
				printf("error exiting %s \n", strerror(errno));
				break;
			}
				
		}
	}*/

	while (true)
	{
		msg = pQueueManager->GetDataFromQueue<MESSAGE>(&inputQueueName, 5000, &cc);
		if (msg.reasonCode == 0 && cc == QUEUE_SUCCESS)
		{
			tmp = msg.ss.s.str();
			msgFrwder.SendData((char*)tmp.c_str(), tmp.length(), &msg.replyIpAddress, &snd_msg);
		}
		else if(cc == QUEUE_SUCCESS)
			printf("Error response from MessageForwarder %d \n", msg.reasonCode);
		

		/* count++;
		if (count > 10000)
			msgFrwder.BeginStop(); 

		if (count > 10000 && cc == QUEUE_TIMEOUT) // just fake doing an actual stop 
		{
			msgFrwder.Stop(10000);
			break;
		} */
	}

	return cc;
}