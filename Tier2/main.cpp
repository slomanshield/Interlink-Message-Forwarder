#include "Tier2.h"

/* Tier 2 main */

/* Tier 2 will route the message based on the contents to the correct tier 3, for testing it will just forward to a generic queue */

int main()
{
	int cc = 0;
	Tier2 tier2;
	int stopRetries = 3;
	std::string input_tier2_msg_queue = "input_tier2_msg_queue";
	size_t input_tier2_msg_queue_size = 0;
	std::string reply_tier2_msg_queue = "reply_tier2_msg_queue";
	size_t reply_tier2_msg_queue_size = 0;
	std::string output_tier3_msg_queue = "output_tier3_msg_queue";
	size_t output_tier3_msg_queue_size = 0;
	std::string reply_tier3_msg_queue = "reply_tier3_msg_queue";
	size_t reply_tier3_msg_queue_size = 0;
	int retries = 0;
	QueueManager* pQueueManager = QueueManager::Instance(); /* just to statically init Queue Manager */
	SetTermHandler();

	if ((cc = tier2.CreateInternalQueue()) == QUEUE_SUCCESS)
	{
		cc = tier2.StartProcessing(20, 20, 25, 25, 100);

		if (cc == 0)
		{

			while (TerminateApplication == false)
			{
				/* pQueueManager->GetQueuesize<MESSAGE>(&input_tier2_msg_queue, &input_tier2_msg_queue_size);
				pQueueManager->GetQueuesize<MESSAGE>(&reply_tier2_msg_queue, &reply_tier2_msg_queue_size);
				pQueueManager->GetQueuesize<MESSAGE>(&output_tier3_msg_queue,&output_tier3_msg_queue_size);
				pQueueManager->GetQueuesize<MESSAGE>(&reply_tier3_msg_queue, &reply_tier3_msg_queue_size); */

				/* printf("Queue Lengths %llu %llu %llu %llu \n ", input_tier2_msg_queue_size, reply_tier2_msg_queue_size,
					output_tier3_msg_queue_size, reply_tier3_msg_queue_size); */


				usleep(10000); /* just need to give up cpu */
			}
		}

		do 
		{
			cc = tier2.StopProcessing();
			if (cc != 0)
			{
				if (cc == connections_exist_on_shutdown)
				{
					printf("Connections exist, waiting... \n");
					usleep(SERVER_UDP_INTERVAL); /* just sleep then check again */
				}
					
				retries++;
				if (retries >= stopRetries)
				{
					printf("Force Stopping Tier 2... \n");
					tier2.StopProcessing(true); /* ok lets force stop probably a missing message */
					cc = 0;
				}
			}
		} while (cc != 0);
		
	}
	else
		printf("Error creating tier 3 queues, exiting with error code %d \n", cc);


	tier2.DestroyInternalQueue();
	QueueManager::DeleteInstance();
	return cc;
}