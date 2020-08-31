#include "Tier1.h"

using namespace OustandingMessages;

Tier1::Tier1()
{
	msgForwarder = nullptr;
	pQueueManager = QueueManager::Instance();
	tier2_queue = TIER2_QUEUE;
	output_msg_queue = "output_tier2_msg_queue";
	reply_msg_queue = "reply_tier2_msg_queue";
	output_queue = "output_tier1_queue";
	timeout_queue = "tier1_timeout_queue";
	recieveDataThreadHandler.SetProcessthread(recieveDataThread);
	processReplyThreadHandler.SetProcessthread(processReplyThread);
	timeoutReplyThreadHandler.SetProcessthread(timeoutReplyThread);
	mapOustandingMessages.SetMaxOutStanding(5000);
}

Tier1::~Tier1()
{
	return;
}

int Tier1::StartProcessing(int numRecieveDataThreads, int numProcessreplyThreads, int numMsgSenders, int numMsgRecievers, int numConnectionsPerHost)
{
	int cc = 0;

	if (msgForwarder == nullptr)
	{
		msgForwarder = new MessageForwarder::MessageForwarder(TIER2_PORT, tier2_queue, reply_msg_queue, output_msg_queue,
			numMsgSenders, numMsgRecievers, numConnectionsPerHost, client, MSG_QUEUE_CONNECTION_TIMEOUT,
			MSG_QUEUE_BUFFER_SIZE);
		msgForwarder->Init();
	}
	
	cc = msgForwarder->Start();
	if(cc != 0)
		printf("Error Tier1::StartProcessing (msgForwarder->Start) with error code %d \n", cc);

	if (cc == 0) /* for the proof of concept just include this here */
	{
		cc = msgForwarder->WaitForConnections(MSG_QUEUE_CONNECTION_TIMEOUT);
		if (cc != 0)
			printf("Error Tier1::StartProcessing (msgForwarder->WaitForConnections) with error code %d \n", cc);
	}

	/* now start all of our threads */
	if (cc == 0)
	{
		cc = StartProcessingThread(&recieveDataThreadHandler, numRecieveDataThreads);
		if(cc != 0)
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
		cc = StartProcessingThread(&timeoutReplyThreadHandler, 1);
		if (cc != 0)
			printf("Error Tier1::StartProcessing (StartProcessingThread(timeoutReplyThreadHandler)) with error code %d \n", cc);
	}

	return cc;
}

int Tier1::StopProcessing()
{
	int cc = 0;
	double dbWaitTimeout = THREAD_WAIT_TIMEOUT + 1000;
	double dbWaitReplys = MSG_QUEUE_CONNECTION_TIMEOUT + 1000;
	if (msgForwarder == nullptr) /* if we dont have a pointer nothing has started */
		return -1;

	cc = pQueueManager->WaitForQueueToDrain<TierMessageInternal>(&output_queue, dbWaitTimeout);/* wait for queue to drain */

	if (cc != SUCCESS)
	{
		size_t queueSize = 0;
		pQueueManager->GetQueuesize<TierMessageInternal>(&output_queue, &queueSize);

		printf("StopProcessing(): Error waiting for %s to drain size is %llu \n", output_queue.c_str(), queueSize);
	}

	if (cc == 0)
	{
		cc = mapOustandingMessages.WaitForOutStandingMessages(dbWaitReplys); /* wait for any messages we need to time out or reply */
		if (cc != 0)
			printf("Error Tier1::StopProcessing waiting for outstanding messages error code %d \n", cc);
	}
	
	if (cc == QUEUE_SUCCESS) /* then lets stop the forwarder */
	{
		cc = msgForwarder->Stop(THREAD_WAIT_TIMEOUT * 2);
		if (cc != 0)
			printf("Error Tier1::StopProcessing (msgForwarder->Stop) with error code %d \n", cc);
	}
	
	if (cc == 0) /* lets wait for any messages from the forwarder */
	{
		cc = pQueueManager->WaitForQueueToDrain<MESSAGE>(&reply_msg_queue, dbWaitTimeout);
		if (cc != QUEUE_SUCCESS)
		{
			size_t queueSize = 0;
			pQueueManager->GetQueuesize<TierMessageInternal>(&reply_msg_queue, &queueSize);

			printf("StopProcessing(): Error waiting for %s to drain size is %llu \n", reply_msg_queue.c_str(), queueSize);
		}
	}
	
	if (cc == QUEUE_SUCCESS) /* ok we should have processed everything lets stop our threads */
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
		cc = StopProcessingThread(&timeoutReplyThreadHandler);
		if (cc != 0)
			printf("Error timeoutReplyThread did not respond to stop \n");
	}

	if (cc == 0)
	{
		if (msgForwarder != nullptr)
		{
			delete msgForwarder;
			msgForwarder = nullptr;
		}
			
	}

	return cc;
}

int Tier1::CreateInternalQueue()
{
	int cc = 0;
	int64_t maxOutStanding = 1000;
	int64_t maxOutStandingReply = UNLIMITED_MAX_OUTSTANDING; /* reply shouldnt be limited just incase */
	int64_t maxOutStandingTimeout = UNLIMITED_MAX_OUTSTANDING;
	int32_t minimumReaders = 1;
	cc = pQueueManager->CreateQueue<MESSAGE>(&output_msg_queue, &maxOutStanding, &minimumReaders);

	if(cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<MESSAGE>(&reply_msg_queue, &maxOutStandingReply, &minimumReaders);
	if(cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<TierMessageInternal>(&output_queue, &maxOutStanding, &minimumReaders);
	if (cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<std::string>(&timeout_queue, &maxOutStandingTimeout, &minimumReaders);

	return cc;
}

void Tier1::DestroyInternalQueue()
{
	pQueueManager->DeleteQueue<MESSAGE>(&output_msg_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&reply_msg_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&output_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&timeout_queue, true);
}

int Tier1::WaitForQueueConnection()
{
	int cc = 0;

	if (msgForwarder != nullptr)
		cc = msgForwarder->WaitForConnections(MSG_QUEUE_CONNECTION_TIMEOUT);
	else
		cc = -1; /* TODO add error code */

	return cc;
}

int Tier1::StartProcessingThread(ThreadWrapper<threadProcess>* pHandle, int numThreads)
{
	int cc = 0;
	bool started = false;

	started = pHandle->StartProcessing(numThreads, this);

	if (started == false)
		cc = -1; /* TODO add error codes */

	return cc;
}

int Tier1::StopProcessingThread(ThreadWrapper<threadProcess>* pHandle)
{
	int cc = 0;
	bool stopped = false;

	stopped = pHandle->StopProcessing(THREAD_WAIT_TIMEOUT + 1000);

	if (stopped == false)
		cc = -1; /* TODO add error codes */

	return cc;
}

void Tier1::recieveDataThread(bool * running, bool * stopped, void * usrPtr)
{
	Tier1* pTier1 = (Tier1*)usrPtr;
	QueueManager* pQueueManager = pTier1->pQueueManager;
	int cc = 0;
	int ccQueue = 0;
	std::string hostIp = "";
	std::string dataMsg;
	TierMessageInternal data;
	StringStreamWrapper ss;
	MESSAGE msg;
	RegisterQueueReadThread(pTier1->output_queue);
	pTier1->msgForwarder->GetHostIp(&hostIp);

	while (*running == true)
	{
		data = pQueueManager->GetDataFromQueue<TierMessageInternal>(&pTier1->output_queue, MSG_QUEUE_CONNECTION_TIMEOUT, &ccQueue);

		if (ccQueue == QUEUE_SUCCESS)
		{
			data.AddReplyIp(&hostIp);
			dataMsg = data.GetJsonData();

			cc = pTier1->mapOustandingMessages.InsertData(data.GetTestDataId(), data, MSG_QUEUE_CONNECTION_TIMEOUT, &pTier1->timeout_queue);
			if (cc == 0)
			{
				cc = pTier1->msgForwarder->SendData((char*)dataMsg.c_str(), dataMsg.length(), nullptr, &msg);

				if (cc != 0)
				{
					printf("Error Sending %s message for error %d \n", data.GetTestDataId().c_str(), cc);
					/* remove from the list and do something */
					pTier1->mapOustandingMessages.RemoveData(data.GetTestDataId());
				}	
			}
			else
				printf("Error MAX OUTSTANDING REACHED for %s message \n", data.GetTestDataId().c_str());
		}
		else if (ccQueue != QUEUE_SUCCESS & ccQueue != QUEUE_TIMEOUT)
			printf("Failed to get data on %s with error %d \n", pTier1->output_queue.c_str(), ccQueue);

		SleepOnQueue(ccQueue);
	}

	RemoveQueueReadThread(pTier1->output_queue);
	*stopped = true;
}

void Tier1::processReplyThread(bool * running, bool * stopped, void * usrPtr)
{
	Tier1* pTier1 = (Tier1*)usrPtr;
	QueueManager* pQueueManager = pTier1->pQueueManager;
	MESSAGE dataMsg;
	int cc = 0;
	int ccQueue = 0;
	RegisterQueueReadThread(pTier1->reply_msg_queue);

	while (*running == true)
	{
		dataMsg = pQueueManager->GetDataFromQueue<MESSAGE>(&pTier1->reply_msg_queue, MSG_QUEUE_CONNECTION_TIMEOUT, &ccQueue);
		
		if (ccQueue == QUEUE_SUCCESS)
		{
			TierMessageInternal data;
			data.SetDataFromJson(dataMsg.ss.s.str());

			cc = pTier1->mapOustandingMessages.RemoveData(data.GetTestDataId());

			if (cc == OustandingMessages::MESSAGE_NOT_FOUND && dataMsg.isResponse == true) /* we got a response and it wasnt on our list */
				printf("Message already consumed logging... test data id is %s \n", data.GetTestDataId().c_str());
			else if (dataMsg.delivered == false && cc == OustandingMessages::SUCCESS) /* failure from forwarder or message timed out */
				printf("Message was not successfully sent or timed out %s \n", data.GetTestDataId().c_str());
			else if (cc == OustandingMessages::MESSAGE_NOT_FOUND) /* highly unlikely but a message that was timed out but the response came just in time */
				printf("Message was timed out but reply message consumed it before hand.. interestng %s \n", data.GetTestDataId().c_str());
			else
			{
				DoubleMicro diff = std::chrono::duration_cast<std::chrono::microseconds>(HighResolutionTime::now() - HighResolutionTimePointClock(std::chrono::nanoseconds(data.GetTimeStampCreate())));
				printf("message recevied test_data_id is %s and timeout is %d sucess_process is %d processing time was %lf \n", data.GetTestDataId().c_str(), data.GetProcessTimeout(), data.GetSucessProcssed(), diff.count());
			}

		}
		else if(ccQueue != QUEUE_SUCCESS & ccQueue != QUEUE_TIMEOUT)
			printf("Failed to get data on %s with error %d \n", pTier1->reply_msg_queue.c_str(), ccQueue);

		SleepOnQueue(ccQueue);
	}

	RemoveQueueReadThread(pTier1->reply_msg_queue);
	*stopped = true;
}

void Tier1::timeoutReplyThread(bool * running, bool * stopped, void * usrPtr)
{
	Tier1* pTier1 = (Tier1*)usrPtr;
	QueueManager* pQueueManager = pTier1->pQueueManager;
	std::string dataMsg = "";
	MESSAGE msg;
	msg.delivered = false;
	std::string msgId = "";
	RegisterQueueReadThread(pTier1->timeout_queue);
	int cc = 0;
	int ccQueue = 0;

	while (*running == true)
	{
		msgId = pQueueManager->GetDataFromQueue<std::string>(&pTier1->timeout_queue, MSG_QUEUE_CONNECTION_TIMEOUT, &ccQueue);

		if (ccQueue == QUEUE_SUCCESS)
		{
			TierMessageInternal data = pTier1->mapOustandingMessages.GetData(msgId, &cc, false); /* dont remove the message so we still have our outstanding */
			if (cc == SUCCESS)
			{
				dataMsg = data.GetJsonData();
				msg.ss.Write((char*)dataMsg.c_str(), dataMsg.length());
				printf("timeoutReplyThread timing out message %s \n", msgId.c_str());
				cc = pQueueManager->PutDataOnQueue<MESSAGE>(&pTier1->reply_msg_queue, msg);

				if (cc != QUEUE_SUCCESS)
					printf("timeoutReplyThread failed to put timed out message on %s queue error %d \n", msgId.c_str(), cc);
			}
			else
				printf("timeoutReplyThread failed to find msgId in outstanding list %s \n", msgId.c_str());
		}
		else if(ccQueue != QUEUE_SUCCESS & ccQueue != QUEUE_TIMEOUT)
			printf("Failed to get data on %s with error %d \n", pTier1->timeout_queue.c_str(), ccQueue);

		SleepOnQueue(ccQueue);
	}

	RemoveQueueReadThread(pTier1->timeout_queue);
	*stopped = true;
}