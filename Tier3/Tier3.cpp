#include "Tier3.h"

Tier3::Tier3()
{
	tier3_queue = TIER3_QUEUE;
	input_tier3_msg_queue = "input_tier3_msg_queue";
	reply_tier3_msg_queue = "reply_tier3_msg_queue";
	pQueueManager = QueueManager::Instance();
	msgForwarderInput = nullptr;
	recieveDataThreadHandler.SetProcessthread(recieveDataThread);
}

Tier3::~Tier3()
{
	return;
}

int Tier3::CreateInternalQueue()
{
	int cc = 0;
	int64_t maxOutStanding = UNLIMITED_MAX_OUTSTANDING; /* should be unlimited because in Tier 1 its mangaged */
	int32_t minimumReaders = 1;
	cc = pQueueManager->CreateQueue<MESSAGE>(&input_tier3_msg_queue, &maxOutStanding, &minimumReaders);

	if (cc == QUEUE_SUCCESS)
		cc = pQueueManager->CreateQueue<MESSAGE>(&reply_tier3_msg_queue, &maxOutStanding, &minimumReaders);

	return cc;
}

void Tier3::DestroyInternalQueue()
{
	pQueueManager->DeleteQueue<MESSAGE>(&input_tier3_msg_queue, true);
	pQueueManager->DeleteQueue<MESSAGE>(&reply_tier3_msg_queue, true);
}

int Tier3::StartProcessingThread(ThreadWrapper<threadProcess>* pHandle, int numThreads)
{
	int cc = 0;
	bool started = false;

	started = pHandle->StartProcessing(numThreads, this);

	if (started == false)
		cc = -1; /* TODO add error codes */

	return cc;
}

int Tier3::StopProcessingThread(ThreadWrapper<threadProcess>* pHandle)
{
	int cc = 0;
	bool stopped = false;

	stopped = pHandle->StopProcessing(THREAD_WAIT_TIMEOUT + 1000);

	if (stopped == false)
		cc = -1; /* TODO add error codes */

	return cc;
}

int Tier3::StartProcessing(int numRecieveDataThreads, int numMsgSenders, int numMsgRecievers, int numConnectionsPerHost)
{
	int cc = 0;

	if (msgForwarderInput == nullptr)
	{
		msgForwarderInput = new MessageForwarder::MessageForwarder(TIER3_PORT, tier3_queue, input_tier3_msg_queue, reply_tier3_msg_queue,
			numMsgSenders, numMsgRecievers, numConnectionsPerHost, server, MSG_QUEUE_CONNECTION_TIMEOUT,
			MSG_QUEUE_BUFFER_SIZE);
		msgForwarderInput->Init();
	}

	if (cc == 0)
	{
		cc = StartProcessingThread(&recieveDataThreadHandler, numRecieveDataThreads);
		if (cc != 0)
			printf("Error Tier1::StartProcessing (StartProcessingThread(recieveDataThreadHandler)) with error code %d \n", cc);
	}

	if (cc == 0)
	{
		cc = msgForwarderInput->Start(); /* everything is ready open the flood gates */
		if (cc != SUCCESS)
			printf("Error msgForwarderInput->Start() with error code %d \n", cc);
	}

	return cc;
}

int Tier3::StopProcessing(bool forceStop)
{
	int cc = 0;
	double dbWaitTimeout = THREAD_WAIT_TIMEOUT + 1000;

	if (msgForwarderInput == nullptr) /* if we dont have a pointer for either no point */
		return -1;

	msgForwarderInput->BeginStop();

	cc = pQueueManager->WaitForQueueToDrain<TierMessageInternal>(&input_tier3_msg_queue, dbWaitTimeout);/* wait for queue to drain */

	if (cc != QUEUE_SUCCESS)
	{
		size_t queueSize = 0;
		pQueueManager->GetQueuesize<TierMessageInternal>(&input_tier3_msg_queue, &queueSize);

		printf("StopProcessing(): Error waiting for %s to drain size is %llu \n", input_tier3_msg_queue.c_str(), queueSize);
	}

	if (cc == 0)
	{
		/* in the event that we can not stop because of lost messages we should force stop after a certain period of time, in this prototype we want to observe these situations */
		cc = msgForwarderInput->Stop(THREAD_WAIT_TIMEOUT * 2, !forceStop, forceStop);
		if (cc != 0)
			printf("Error Tier1::StopProcessing (msgForwarderInput->Stop) with error code %d \n", cc);
	}

	if (cc == 0)
	{
		cc = StopProcessingThread(&recieveDataThreadHandler);
		if (cc != 0)
			printf("Error recieveDataThread did not respond to stop \n");
	}

	if (cc == 0)
	{
		if (msgForwarderInput != nullptr)
		{
			delete msgForwarderInput;
			msgForwarderInput = nullptr;
		}
	}

	return cc;
}

void Tier3::recieveDataThread(bool * running, bool * stopped, void * usrPtr)
{
	Tier3* pTier3 = (Tier3*)usrPtr;
	QueueManager* pQueueManager = pTier3->pQueueManager;
	MESSAGE dataMsg;
	MESSAGE msg;
	TierMessageInternal data;
	std::string replyIp = "";
	std::string dataJson = "";
	int cc = 0;
	int ccQueue = 0;
	RegisterQueueReadThread(pTier3->input_tier3_msg_queue);

	while (*running == true)
	{
		dataMsg = pQueueManager->GetDataFromQueue<MESSAGE>(&pTier3->input_tier3_msg_queue, MSG_QUEUE_CONNECTION_TIMEOUT, &ccQueue);

		if (ccQueue == QUEUE_SUCCESS) /* we got data lets just reply  */
		{
			if (dataMsg.reasonCode == 0)
			{
				/* in here we would do some business logic then forward to the correct thread group based on type */
				data.SetDataFromJson(dataMsg.ss.s.str()); /* lets get our reply Ip */
				replyIp = data.GetLastReplyIp(true);
				data.SetSuccessProcess(true);
				dataJson = data.GetJsonData();

				cc = pTier3->msgForwarderInput->SendData((char*)dataJson.c_str(), dataJson.length(), &replyIp, &msg);

				if (cc != 0)
				{
					printf("Error Sending %s message for error %d \n", data.GetTestDataId().c_str(), cc);
				}
			}
			else
				printf("Error message from msgForwarderInput %d \n", dataMsg.reasonCode);

		}
		else if (ccQueue != QUEUE_SUCCESS && ccQueue != QUEUE_TIMEOUT)
			printf("Failed to get data on %s with error %d \n", pTier3->input_tier3_msg_queue.c_str(), ccQueue);

		SleepOnQueue(ccQueue);
	}

	RemoveQueueReadThread(pTier3->input_tier3_msg_queue);
	*stopped = true;
}