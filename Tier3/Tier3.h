#ifndef TIER3_H
#define TIER3_H
#include "../Global/MessageForwarder.h"
#include "../Global/MsgQueueDefines.h"

using namespace OustandingMessages;
using namespace MessageForwarder;

/* really should make this a base class but eh that will be for actual implementation */

class Tier3
{
public:
	Tier3();
	~Tier3();
	int CreateInternalQueue();
	int StartProcessing(int numRecieveDataThreads, int numMsgSenders, int numMsgRecievers, int numConnectionsPerHost);
	int StopProcessing(bool forceStop = false);
	void DestroyInternalQueue();
private:

	int StartProcessingThread(ThreadWrapper<threadProcess>* pHandle, int numThreads);
	int StopProcessingThread(ThreadWrapper<threadProcess>* pHandle);

	static threadProcess recieveDataThread;
	ThreadWrapper<threadProcess> recieveDataThreadHandler;

	QueueManager* pQueueManager;
	MessageForwarder::MessageForwarder* msgForwarderInput;

	std::string tier3_queue;
	std::string input_tier3_msg_queue;
	std::string reply_tier3_msg_queue;
};


#endif