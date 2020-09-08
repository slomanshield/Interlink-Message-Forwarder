#include "Tier2.h"
#include "../Global/InternalMsgTLV.h"

Tier2::Tier2()
{
	msgForwarderInput = nullptr;
	msgForwarderOutput = nullptr;
	pQueueManager = QueueManager::Instance();
	tier2_queue = TIER2_QUEUE;
	tier3_queue = TIER3_QUEUE;
	input_tier2_msg_queue = "input_tier2_msg_queue";
	reply_tier2_msg_queue = "reply_tier2_msg_queue";
	output_tier3_msg_queue = "output_tier3_msg_queue";
	reply_tier3_msg_queue = "reply_tier3_msg_queue";
	recieveDataThreadHandler.SetProcessthread(recieveDataThread);
	processReplyThreadHandler.SetProcessthread(processReplyThread);
}

Tier2::~Tier2()
{
	return;
}

int Tier2::CreateInternalQueue()
{
	int cc = 0;
	int64_t maxOutStanding = UNLIMITED_MAX_OUTSTANDING; /* should be unlimited because in Tier 1 its mangaged */
	int32_t minimumReaders = 1;
	cc = pQueueManager->CreateQueue<MESSAGE>(&input_tier2_msg_queue, &maxOutStanding, &minimumReaders);

	if (cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<MESSAGE>(&reply_tier2_msg_queue, &maxOutStanding, &minimumReaders);
	if (cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<MESSAGE>(&output_tier3_msg_queue, &maxOutStanding, &minimumReaders);
	if(cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<MESSAGE>(&reply_tier3_msg_queue, &maxOutStanding, &minimumReaders);

	return cc;
}

void Tier2::DestroyInternalQueue()
{
	pQueueManager->DeleteQueue<MESSAGE>(&input_tier2_msg_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&reply_tier2_msg_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&output_tier3_msg_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&reply_tier3_msg_queue, true);
}

int Tier2::StartProcessing(int numRecieveDataThreads, int numProcessreplyThreads, int numMsgSenders, int numMsgRecievers, int numConnectionsPerHost)
{
	int cc = 0;

	if (msgForwarderInput == nullptr)
	{
		msgForwarderInput = new MessageForwarder::MessageForwarder(TIER2_PORT, tier2_queue, input_tier2_msg_queue, reply_tier2_msg_queue,
			numMsgSenders, numMsgRecievers, numConnectionsPerHost, server, MSG_QUEUE_CONNECTION_TIMEOUT,
			MSG_QUEUE_BUFFER_SIZE);
		msgForwarderInput->Init();
	}

	if (msgForwarderOutput == nullptr)
	{
		msgForwarderOutput = new MessageForwarder::MessageForwarder(TIER3_PORT, tier3_queue, reply_tier3_msg_queue, output_tier3_msg_queue,
			numMsgSenders, numMsgRecievers, numConnectionsPerHost, client, MSG_QUEUE_CONNECTION_TIMEOUT,
			MSG_QUEUE_BUFFER_SIZE);
		msgForwarderOutput->Init();
	}

	cc = msgForwarderOutput->Start(); /* lets try to connect to a server */

	if (cc != SUCCESS)
		printf("Error msgForwarderOutput->Start() with error code %d \n", cc);

	if (cc == 0) /* lets wait to see if we have any servers */
	{
		cc = msgForwarderOutput->WaitForConnections(MSG_QUEUE_CONNECTION_TIMEOUT);
		if (cc != 0)
			printf("Error Tier2::StartProcessing (msgForwarder->WaitForConnections) with error code %d \n", cc);
	}

	if (cc == 0)
	{
		cc = StartProcessingThread(&recieveDataThreadHandler, numRecieveDataThreads);
		if (cc != 0)
			printf("Error Tier1::StartProcessing (StartProcessingThread(recieveDataThreadHandler)) with error code %d \n", cc);
	}
	if (cc == 0)
	{
		cc = StartProcessingThread(&processReplyThreadHandler, numProcessreplyThreads);
		if (cc != 0)
			printf("Error Tier1::StartProcessing (StartProcessingThread(processReplyThreadHandler)) with error code %d \n", cc);
	}

	if (cc == 0)
	{
		cc = msgForwarderInput->Start(); /* everything is ready open the flood gates */
		if(cc != SUCCESS)
			printf("Error msgForwarderInput->Start() with error code %d \n", cc);
	}

}

/* in the main we might need to call this multiple times while messages are being sent after n tries we should shut down forcefully */
int Tier2::StopProcessing(bool forceStop)
{
	int cc = 0;
	double dbWaitTimeout = THREAD_WAIT_TIMEOUT + 1000;
	double dbWaitReplys = MSG_QUEUE_CONNECTION_TIMEOUT + 1000;

	if (msgForwarderInput == nullptr || msgForwarderOutput == nullptr) /* if we dont have a pointer for either no point */
		return -1;

	msgForwarderInput->BeginStop(); /* while we wait for messages lets start telling clients to stop, this could be called in main  */

	cc = pQueueManager->WaitForQueueToDrain<TierMessageInternal>(&input_tier2_msg_queue, dbWaitTimeout);/* wait for queue to drain */

	if (cc != QUEUE_SUCCESS)
	{
		size_t queueSize = 0;
		pQueueManager->GetQueuesize<TierMessageInternal>(&input_tier2_msg_queue, &queueSize);

		printf("StopProcessing(): Error waiting for %s to drain size is %llu \n", input_tier2_msg_queue.c_str(), queueSize);
	}

	/* In an actual application we would call being stop then all other Objects would call stop before calling a full stop on the input */

	if (cc == 0)
	{
		/* in the event that we can not stop because of lost messages we should force stop after a certain period of time, in this prototype we want to observe these situations */
		cc = msgForwarderInput->Stop(THREAD_WAIT_TIMEOUT * 2, !forceStop, forceStop);
		if (cc != 0)
			printf("Error Tier1::StopProcessing (msgForwarderInput->Stop) with error code %d \n", cc);
	}

	if (cc == 0) /* if we sucessfully stopped the server we should have no outstanding messages in the Output so lets just force stop */
	{
		cc = msgForwarderOutput->Stop(THREAD_WAIT_TIMEOUT * 2,false);
		if (cc != 0)
			printf("Error Tier1::StopProcessing (msgForwarderOutput->Stop) with error code %d \n", cc);
	}

	if (cc == 0)
	{
		cc = StopProcessingThread(&recieveDataThreadHandler);
		if (cc != 0)
			printf("Error recieveDataThread did not respond to stop \n");
	}
	if (cc == 0)
	{
		cc = StopProcessingThread(&processReplyThreadHandler);
		if (cc != 0)
			printf("Error processReplyThread did not respond to stop \n");
	}

	if (cc == 0)
	{
		if (msgForwarderInput != nullptr)
		{
			delete msgForwarderInput;
			msgForwarderInput = nullptr;
		}
			
		if (msgForwarderOutput != nullptr)
		{
			delete msgForwarderOutput;
			msgForwarderOutput = nullptr;
		}
			
	}

	return cc;
}

int Tier2::StartProcessingThread(ThreadWrapper<threadProcess>* pHandle, int numThreads)
{
	int cc = 0;
	bool started = false;

	started = pHandle->StartProcessing(numThreads, this);

	if (started == false)
		cc = -1; /* TODO add error codes */

	return cc;
}

int Tier2::StopProcessingThread(ThreadWrapper<threadProcess>* pHandle)
{
	int cc = 0;
	bool stopped = false;

	stopped = pHandle->StopProcessing(THREAD_WAIT_TIMEOUT + 1000);

	if (stopped == false)
		cc = -1; /* TODO add error codes */

	return cc;
}

void Tier2::recieveDataThread(bool * running, bool * stopped, void * usrPtr)
{
	Tier2* pTier2 = (Tier2*)usrPtr;
	QueueManager* pQueueManager = pTier2->pQueueManager;
	MESSAGE dataMsg;
	MESSAGE msg;
	InternalMsgTLV data;
	int cc = 0;
	int ccQueue = 0;
	std::string hostIp = "";
	std::string TLVdata = "";
	RegisterQueueReadThread<MESSAGE>(pTier2->input_tier2_msg_queue);
	pTier2->msgForwarderOutput->GetHostIp(&hostIp);

	while (*running == true)
	{
		dataMsg = pQueueManager->GetDataFromQueue<MESSAGE>(&pTier2->input_tier2_msg_queue, MSG_QUEUE_CONNECTION_TIMEOUT, &ccQueue);

		if (ccQueue == QUEUE_SUCCESS) /* we got data lets just forward */
		{
			if (dataMsg.reasonCode == 0)
			{
				/* in here we would do some business logic then forward to the correct thread group based on type */
				data.SetDataFromTLV((char*)dataMsg.ss.c_str(), dataMsg.ss.length());
				data.AddReplyIp(&hostIp);
				data.GetTLVFromData(&TLVdata);

				cc = pTier2->msgForwarderOutput->SendData((char*)TLVdata.c_str(), TLVdata.length(), nullptr, &msg);

				if (cc != 0)
				{
					printf("Error Sending %s message for error %d \n", data.GetTestDataId().c_str(), cc);
				}
			}
			else
				printf("Error message from msgForwarderInput %d \n", dataMsg.reasonCode);

		}
		else if(ccQueue != QUEUE_SUCCESS & ccQueue != QUEUE_TIMEOUT)
			printf("Failed to get data on %s with error %d \n", pTier2->input_tier2_msg_queue.c_str(), ccQueue);

		SleepOnQueue(ccQueue);
	}

	RemoveQueueReadThread<MESSAGE>(pTier2->input_tier2_msg_queue);
	*stopped = true;
}

void Tier2::processReplyThread(bool * running, bool * stopped, void * usrPtr)
{
	Tier2* pTier2 = (Tier2*)usrPtr;
	QueueManager* pQueueManager = pTier2->pQueueManager;
	MESSAGE dataMsg;
	MESSAGE msg;
	InternalMsgTLV data;
	std::string replyIp = "";
	std::string TLVdata = "";
	int cc = 0;
	int ccQueue = 0;
	RegisterQueueReadThread<MESSAGE>(pTier2->reply_tier3_msg_queue);

	while (*running == true)
	{
		dataMsg = pQueueManager->GetDataFromQueue<MESSAGE>(&pTier2->reply_tier3_msg_queue, MSG_QUEUE_CONNECTION_TIMEOUT, &ccQueue);

		if (ccQueue == QUEUE_SUCCESS) /* we got data lets just forward */
		{
			if (dataMsg.reasonCode == 0)
			{
				data.SetDataFromTLV((char*)dataMsg.ss.c_str(), dataMsg.ss.length());
				replyIp = data.GetLastReplyIp(true);
				data.GetTLVFromData(&TLVdata);

				/* in our implementation we have the reply ip in the data you can use something like outstanding message tracker to keep the data needed for reply */
				cc = pTier2->msgForwarderInput->SendData((char*)TLVdata.c_str(), TLVdata.length(), &replyIp, &msg);

				if (cc != 0)
					printf("Error Sending %s message for error %d \n", data.GetTestDataId().c_str(), cc);
			}
			else
				printf("Error message from msgForwarderOutput %d \n", dataMsg.reasonCode);

		}
		else if (ccQueue != QUEUE_SUCCESS & ccQueue != QUEUE_TIMEOUT)
			printf("Failed to get data on %s with error %d \n", pTier2->reply_tier3_msg_queue.c_str(), ccQueue);

		SleepOnQueue(ccQueue);
	}

	RemoveQueueReadThread<MESSAGE>(pTier2->reply_tier3_msg_queue);
	*stopped = true;
}