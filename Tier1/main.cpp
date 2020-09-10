#include "Tier1.h"

/* Tier 1 main */

/* tier 1 is the originator of the request will have a data generator thread for testing in the main */

int main(int arc, char** argv)
{
	int cc = 0;
	std::string output_queue = "output_tier1_queue";
	std::string padding = "";
	QueueManager* pQueueManager = QueueManager::Instance(); /* just to statically init Queue Manager */
	SetTermHandler();
	Tier1 tier1;

	for (int i = 0; i < 50000; i++)
	{
		padding.append("0"); /* do this to change payload size from typical ~300 to simulate larger payloads */
	}

	if ((cc = tier1.CreateInternalQueue()) == QUEUE_SUCCESS)
	{
		cc = tier1.StartProcessing(20, 20, 25, 25, 400);

		if (cc == 0)
		{
			InternalMsgTLV internalMessage;
			std::string uuid;

			internalMessage.SetPadding(&padding);

			while (TerminateApplication == false)
			{
				internalMessage.SetProcessTimeout(false);
				uuid = GetUuid();
				internalMessage.SetTestDataId(uuid);
				internalMessage.SetTimeStampCreate(HighResolutionTime::now().time_since_epoch().count());

				cc = pQueueManager->PutDataOnQueue<InternalMsgTLV>(&output_queue, internalMessage);

				if (cc != QUEUE_SUCCESS)
					printf("Error sending message onto %s with error code %d \n", output_queue.c_str(), cc);
				usleep(1000000);
			}
		}

		tier1.StopProcessing();
	}
	else
		printf("Error creating tier 1 queues, exiting with error code %d \n", cc);
	
	tier1.DestroyInternalQueue();
	QueueManager::DeleteInstance();
	return cc;
}