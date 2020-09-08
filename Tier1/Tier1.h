#ifndef TIER1_H
#define TIER1_H
#include "../Global/MessageForwarder.h"
#include "../Global/MsgQueueDefines.h"
#include "../Global/OutstandingMessageTracker.h"
#include "../Global/InternalMsgTLV.h"

using namespace OustandingMessages;
using namespace MessageForwarder;

class Tier1
{
	public:
		Tier1();
		~Tier1();
		int CreateInternalQueue();
		int StartProcessing(int numRecieveDataThreads, int numProcessreplyThreads, int numMsgSenders, int numMsgRecievers, int numConnectionsPerHost);
		int StopProcessing();
		int WaitForQueueConnection();
		void DestroyInternalQueue();
	private:

		int StartProcessingThread(ThreadWrapper<threadProcess>* pHandle,int numThreads);
		int StopProcessingThread(ThreadWrapper<threadProcess>* pHandle);

		static threadProcess recieveDataThread;
		static threadProcess processReplyThread;
		static threadProcess timeoutReplyThread;
		ThreadWrapper<threadProcess> recieveDataThreadHandler;
		ThreadWrapper<threadProcess> processReplyThreadHandler;
		ThreadWrapper<threadProcess> timeoutReplyThreadHandler;
		QueueManager* pQueueManager;
		OustandingMessageTracker< InternalMsgTLV, std::string, 1000> mapOustandingMessages;
		MessageForwarder::MessageForwarder* msgForwarder;

		std::string tier2_queue;
		std::string output_msg_queue;
		std::string reply_msg_queue;
		std::string output_queue;
		std::string timeout_queue;
};



#endif // !TIER1_H

