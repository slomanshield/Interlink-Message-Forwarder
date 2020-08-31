#include "../Global/MessageForwarder.h"

using namespace MessageForwarder;

/* main client */

void ReplyThread(bool* running, bool* stopped, void* usrPtr)
{
	QueueManager* pQueueManager = QueueWrapper::QueueManager::Instance();
	std::string replyQueueName = "reply_queue";
	std::string dataStr = "";
	int cc = 0;
	MESSAGE msg;
	RegisterQueueReadThread(replyQueueName);

	while (*running == true)
	{
		msg = pQueueManager->GetDataFromQueue<MESSAGE>(&replyQueueName, 5000, &cc);
		if (msg.reasonCode == 0)
		{
			dataStr = msg.ss.s.str();
			if (cc == QUEUE_SUCCESS)
				printf("Message Recieved Starting Byte Data is 0x%02X \n", *(dataStr.c_str()));
		}
		else
			printf("Error response from MessageForwarder %d \n", msg.reasonCode);

		SleepOnQueue(cc);
	}
	*stopped = true;
	RemoveQueueReadThread(replyQueueName);
}

int main(int argc, char** argv)
{
	int cc = 0;
	int64_t numOutStanding = -1;
	int32_t numReaders = 1;
	std::string outputQueueName = "output_queue";
	std::string replyQueueName = "reply_queue";
	QueueManager* pQueueManager = QueueWrapper::QueueManager::Instance();
	pQueueManager->CreateQueue<MESSAGE>(&outputQueueName, &numOutStanding, &numReaders);
	pQueueManager->CreateQueue<MESSAGE>(&replyQueueName, &numOutStanding, &numReaders);
	TcpBaseFuncs tcpBaseFuncs;
	UdpBaseFuncs udpBasefuncs;
	int sockFd = 0;
	int connFd = 0;
	int dgramSocket = 0;
	int epollListenFd = 0;
	std::string peer_ip = "";
	bool timeout = false;
	bool connected = false;
	struct epoll_event event = { 0 };
	int epollFd = epoll_create1(0);
	int epollDgramfd = epoll_create1(0);
	uint64_t lengthRead = 0;
	char buffer[600000] = { 0 };
	char buffer1[100] = { '1','2','3','4','a','b','c' };
	HighResolutionTimePointClock curr;
	HighResolutionTimePointClock prev;
	DoubleMicro diff;
	StringStreamWrapper s;
	StringStreamWrapper s2;
	std::string replyIp = "";
	MESSAGE m;
	size_t size = 0;
	int i = 0;
	double avg = 0;
	double total = 0;
	uint64_t count = 0;
	uint64_t lengthSent = 0;
	std::mutex cv_m;
	std::unique_lock<std::mutex> lk(cv_m);
	std::condition_variable_any cv;
	ThreadWrapper <threadProcess> replyThreadHandler;
	replyThreadHandler.SetProcessthread(ReplyThread);
	replyThreadHandler.StartProcessing(1, nullptr);
	MessageForwarder::MessageForwarder msgFrwder(30000, "test_queue", "reply_queue", "output_queue", 20, 20, 50, client, 5000, 50000000);
	msgFrwder.Init();
	msgFrwder.Start();



	SetTermHandler();

	/*udpBasefuncs.CreateDgramSocket(true, &dgramSocket);
	while (true)
	{
		udpBasefuncs.SendToMultiCast(""226.20.1.1", dgramSocket, 30000, (unsigned char*)"hello", 5);
		usleep(10000);
	}*/

	/*tcpBaseFuncs.Connect("test", 30000,&connFd, true);
	event.data.fd = connFd;
	tcpBaseFuncs.AddSocketToEpoll(connFd, epollFd, &event);
	do
	{
		cc = tcpBaseFuncs.IsWritable(connFd, epollFd, &connected, &event);
		usleep(5000);

	} while (connected == false && cc == 0);

	if (cc != 0)
	{
		cc = tcpBaseFuncs.CloseFd(&connFd);
	}
	else
	{
		tcpBaseFuncs.SetIOBufferSize(connFd, 10000000, 10000000);

		while (true)
		{
			tcpBaseFuncs.Send(connFd, (unsigned char *)buffer, sizeof(buffer), &lengthSent);
			usleep(100);
		}

	} */
	msgFrwder.WaitForConnections(10000);
	s.Write(buffer, sizeof(buffer));
	s2 = s;
	
	
	while (true)
	{
//		prev = HighResolutionTime::now();
		msgFrwder.SendData(buffer, sizeof(buffer), &replyIp,&m);
//		curr = HighResolutionTime::now();
/*		diff = std::chrono::duration_cast<std::chrono::microseconds>(curr - prev);
		total += diff.count();
		count++;
		avg = total / count; */
		usleep(100);
		pQueueManager->GetQueuesize<MESSAGE>(&outputQueueName, &size);
		/* i++;
		if (i > 1000)
			break; */
		printf("Queue size is %llu \n", size);
	}
	msgFrwder.Stop(10000, true);

    return cc;
}