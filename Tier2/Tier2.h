#ifndef TIER2_H
#define TIER2_H

#include "../Global/MessageForwarder.h"
#include "../Global/MsgQueueDefines.h"

using namespace OustandingMessages;
using namespace MessageForwarder;

/* for the prototype this will be treated as just a middle leg, in reality this class will manage member classes from a baseclass
   and for each type the num threads senders etc should be managed from a config file or database entries based on type for now just to test "timing"
   we will treat this like Tier 1 just with a output and input msg forwdr. This would add an extra hop (in and out) from the object of its threads and queues, but its roughly 0-10 microseconds
*/

class Tier2
{
public:
	Tier2();
	~Tier2();
	int CreateInternalQueue();
	int StartProcessing(int numRecieveDataThreads, int numProcessreplyThreads, int numMsgSenders, int numMsgRecievers, int numConnectionsPerHost);
	int StopProcessing(bool forceStop = false);
	void DestroyInternalQueue();
private:

	int StartProcessingThread(ThreadWrapper<threadProcess>* pHandle, int numThreads);
	int StopProcessingThread(ThreadWrapper<threadProcess>* pHandle);

	static threadProcess recieveDataThread;
	static threadProcess processReplyThread;
	ThreadWrapper<threadProcess> recieveDataThreadHandler;
	ThreadWrapper<threadProcess> processReplyThreadHandler;
	QueueManager* pQueueManager;
	MessageForwarder::MessageForwarder* msgForwarderInput;
	MessageForwarder::MessageForwarder* msgForwarderOutput;

	std::string tier3_queue;
	std::string tier2_queue;
	std::string reply_tier2_msg_queue;
	std::string input_tier2_msg_queue;
	std::string reply_tier3_msg_queue;
	std::string output_tier3_msg_queue;
};


#endif // !TIER2_H