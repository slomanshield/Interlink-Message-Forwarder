#include "Tier3.h"

/* Tier 3 main */

/* tier 3 will process the message correctly, for testing purposes it will set a flag and return it back */

int main()
{
	int cc = 0;
	Tier3 tier3;
	int stopRetries = 3;
	int retries = 0;
	std::string input_tier3_msg_queue = "input_tier3_msg_queue";
	size_t input_tier3_msg_queue_size = 0;
	std::string reply_tier3_msg_queue = "reply_tier3_msg_queue";
	size_t reply_tier3_msg_queue_size = 0;
	QueueManager* pQueueManager = QueueManager::Instance(); /* just to statically init Queue Manager */
	SetTermHandler();


	if ((cc = tier3.CreateInternalQueue()) == QUEUE_SUCCESS)
	{
		cc = tier3.StartProcessing(20, 25, 25, 200);

		if (cc == 0)
		{

			while (TerminateApplication == false)
			{
				/* pQueueManager->GetQueuesize<MESSAGE>(&input_tier3_msg_queue, &input_tier3_msg_queue_size);
				pQueueManager->GetQueuesize<MESSAGE>(&reply_tier3_msg_queue, &input_tier3_msg_queue_size); */
				/* printf("Queue Lengths %llu %llu \n ", input_tier3_msg_queue_size, input_tier3_msg_queue_size); */
				usleep(10000); /* just need to give up cpu */
			}
		}

		do
		{
			cc = tier3.StopProcessing();
			if (cc != 0)
			{
				if (retries >= stopRetries)
				{
					retries++;
					printf("Force Stopping Tier 3... \n");
					tier3.StopProcessing(true); /* ok lets force stop probably a missing message */
					cc = 0;
				}
			}
		} while (cc != 0);

	}
	else
		printf("Error creating tier 3 queues, exiting with error code %d \n", cc);


	tier3.DestroyInternalQueue();
	QueueManager::DeleteInstance();

	return cc;
}