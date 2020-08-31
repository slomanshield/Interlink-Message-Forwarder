
/* template implementation */
namespace QueueWrapper {
	template<typename T>
	T QueueWrapper::QueueManager::GetDataFromQueue(std::string* queueName, int milliTimeOut, int* ccOut)
	{
		*ccOut = QUEUE_SUCCESS;
		bool timeout = true;
		Queue<T>* pQueue = nullptr;
		T retData;

		*ccOut = FindQueue(queueName, &pQueue);

		if (*ccOut == QUEUE_SUCCESS)
		{
			std::unique_lock<std::mutex>lk(pQueue->m);

			timeout = pQueue->WaitForQueueData(milliTimeOut,&lk);

			if (timeout == false)//we got a true event notification
				pQueue->DecrementEventCount();

			if (timeout == true && !pQueue->mainQueue.empty())//data on queue but condition hasnt flipped fake it
				timeout = false;

			if (timeout == false && !pQueue->mainQueue.empty())//we have data
			{
				retData = pQueue->mainQueue.front();
				pQueue->mainQueue.pop();
				*ccOut = QUEUE_SUCCESS;
			}

			if (timeout == true)//if we timed out on the condition and no data in queue
				*ccOut = QUEUE_TIMEOUT;

			lk.unlock();

		}


		return retData;
	}

	template<typename T>
	int QueueWrapper::QueueManager::PutDataOnQueue(std::string* queueName, T data)
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		cc = FindQueue(queueName, &pQueue);

		if (cc == QUEUE_SUCCESS)
		{
			cc = ReadersExistForQueue(pQueue);
			if (cc == QUEUE_SUCCESS)
			{
				std::unique_lock<std::mutex>lk(pQueue->m);

				if (pQueue->GetMaxOutStanding() != UNLIMITED_MAX_OUTSTANDING && pQueue->mainQueue.size() > pQueue->GetMaxOutStanding())
					cc = QUEUE_MAX_OUTSTANDING;
				else
				{
					pQueue->mainQueue.push(data);
					pQueue->IncrementEventCount();
					pQueue->cv.notify_one();
				}

				lk.unlock();
			}
			
		}

		return cc;
	}

	template<typename T>
	int QueueWrapper::QueueManager::GetQueuesize(std::string * queueName, size_t* queueSizeOut)
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		cc = FindQueue(queueName, &pQueue);

		if (cc == QUEUE_SUCCESS)
		{
			*queueSizeOut = pQueue->mainQueue.size();
		}

		return cc;
	}

	template<typename T>
	int QueueWrapper::QueueManager::CreateQueue(std::string* queueName, int64_t* pMaxOutstanding, int32_t* pMinimumReadersReq)//-1 is no max outstanding
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		std::unique_lock<std::mutex> lk(m_queues);
		cc = FindQueue(queueName, &pQueue);//call should fail

		if (cc == QUEUE_SUCCESS)
			cc = QUEUE_ALREADY_DEFINED;
		else
		{
			if (pMaxOutstanding != nullptr && pMinimumReadersReq != nullptr)
				pQueue = new Queue<T>(*pMaxOutstanding, *pMinimumReadersReq);
			else
				pQueue = new Queue<T>();

			queueMap.insert({ *queueName, (void *)pQueue });
			cc = QUEUE_SUCCESS;
		}
		lk.unlock();

		return cc;
	}

	template<typename T>
	int QueueWrapper::QueueManager::DeleteQueue(std::string * queueName, bool outstandingOverride)
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		std::unique_lock<std::mutex> lk(m_queues);
		cc = FindQueue(queueName, &pQueue);//call should succeed

		if (cc == QUEUE_SUCCESS)
		{
			if (!pQueue->mainQueue.empty() && outstandingOverride == false)
				cc = QUEUE_HAS_DATA;
			else
			{
				queueMap.erase(*queueName);
				delete pQueue;
			}
		}
		lk.unlock();

		if (cc == QUEUE_NOT_FOUND)//if it doesnt exist fake it
			cc = QUEUE_SUCCESS;

		return cc;
	}

	template<typename T>
	int QueueManager::RegisterThreadToQueue(std::string * queueName, std::thread::id threadId)
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		cc = FindQueue(queueName, &pQueue);

		if (cc == QUEUE_SUCCESS)
		{
			ThreadIdListInterator itr;
			std::unique_lock<std::mutex>lk(pQueue->m_ThreadIdList);

			itr = pQueue->FindThreadId(threadId);

			if (itr == pQueue->readerThreadIdList.end())//should not exist
				pQueue->readerThreadIdList.push_back(threadId);
			else
				cc = THREAD_ID_EXISTS;

			lk.unlock();
		}

		return cc;
	}

	template<typename T>
	int QueueManager::RemoveThreadFromQueue(std::string * queueName, std::thread::id threadId)
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		cc = FindQueue(queueName, &pQueue);

		if (cc == QUEUE_SUCCESS)
		{
			ThreadIdListInterator itr;
			std::unique_lock<std::mutex>lk(pQueue->m_ThreadIdList);

			itr = pQueue->FindThreadId(threadId);

			if (itr != pQueue->readerThreadIdList.end())//should exist
				pQueue->readerThreadIdList.erase(itr);
			else
				cc = THREAD_ID_NOT_FOUND;

			lk.unlock();
		}

		if (cc == THREAD_ID_NOT_FOUND)//if we didnt find it fake a success because we are removing
			cc = QUEUE_SUCCESS;

		return cc;
	}

	template<typename T>
	int QueueManager::ReadersExistForQueue(Queue<T>* pQueue)
	{
		int cc = QUEUE_SUCCESS;

		size_t num_readers = pQueue->readerThreadIdList.size();

		if (num_readers > 0)
		{
			if (num_readers < pQueue->GetMinimumReaders())
				cc = NO_MINIMUM_READERS_FOR_QUEUE;
		}
		else
			cc = NO_READERS_FOR_QUEUE;
		

		return cc;
	}

	template<typename T>
	int QueueWrapper::QueueManager::FindQueue(std::string* queueName, Queue<T>** pQueueOut)
	{
		int cc = QUEUE_SUCCESS;

		QueueIterator queueIterator = queueMap.find(*queueName);

		if (queueIterator != queueMap.end())
			*pQueueOut = (Queue<T>*)(*queueIterator).second;
		else
		{
			*pQueueOut = nullptr;
			cc = QUEUE_NOT_FOUND;
		}


		return cc;
	}

	template<typename T>
	int  QueueWrapper::QueueManager::WaitForQueueToDrain(std::string* queueName, double numMilliSeconds)
	{
		int cc = QUEUE_SUCCESS;
		Queue<T>* pQueue = nullptr;

		cc = FindQueue(queueName, &pQueue);

		if (cc == QUEUE_SUCCESS)
		{
			HighResolutionTimePointClock startTime = HighResolutionTime::now();
			HighResolutionTimePointClock currentTime = startTime;
			DoubleMili diff;
			while (!pQueue->mainQueue.empty() && cc == QUEUE_SUCCESS)
			{
				usleep(1000); /* sleep for 1 milli second to give up CPU */
				currentTime = HighResolutionTime::now();
				diff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
				if (diff.count() > numMilliSeconds)
					cc = WAIT_DRAIN_TIMEOUT;
					
			}
		}

		return cc;
	}

	template<typename T>
	QueueWrapper::Queue<T>::Queue(int64_t max_out_standing, int32_t minimum_readers_req)
	{
		this->max_out_standing = max_out_standing;
		if (minimum_readers_req >= DEFAULT_MINIMUM_READERS)
			this->minimum_readers_req = minimum_readers_req;
		else
			this->minimum_readers_req = DEFAULT_MINIMUM_READERS;
		event_count = 0;
	}


	template<typename T>
	QueueWrapper::Queue<T>::Queue()
	{
		max_out_standing = DEFAULT_MAX_OUTSTANDING;
		minimum_readers_req = DEFAULT_MINIMUM_READERS;
		event_count = 0;
	}

	template<typename T>
	QueueWrapper::Queue<T>::~Queue()
	{
		readerThreadIdList.erase(readerThreadIdList.begin(), readerThreadIdList.end());
	}

	template<typename T>
	bool QueueWrapper::Queue<T>::WaitForQueueData(int milliTimeOut, std::unique_lock<std::mutex>* pLK)
	{
		bool timeout = true;

		timeout = !cv.wait_for(*pLK, Milli(milliTimeOut), [this] {return event_count > 0; });

		return timeout;
	}

	template<typename T>
	void QueueWrapper::Queue<T>::IncrementEventCount()
	{
		uint64_t prev_event_count = event_count;
		event_count++;
		if (event_count == 0 && prev_event_count == 0xFFFFFFFFFFFFFFFF)
			event_count = 0xFFFFFFFFFFFFFFFF;//this means data is being put on the queue faster than we can read... by this point we possibly would run out of memory and other alarms should go off  
		/* but what this does is make it so we can at least take off all the data */
	}

	template<typename T>
	void QueueWrapper::Queue<T>::DecrementEventCount()
	{
		event_count--;
		if (event_count == 0xFFFFFFFFFFFFFFFF)
			event_count = 0;//if we are going below that means we are taking things off and threads getting notified before they should be
	}

	template<typename T>
	int64_t QueueWrapper::Queue<T>::GetMaxOutStanding()
	{
		return max_out_standing;
	}

	template<typename T>
	int32_t QueueWrapper::Queue<T>::GetMinimumReaders()
	{
		return minimum_readers_req;
	}

	template<typename T>
	ThreadIdListInterator Queue<T>::FindThreadId(std::thread::id threadId)
	{
		ThreadIdListInterator itr = readerThreadIdList.begin();

		for (; itr != readerThreadIdList.end(); itr++)
		{
			if (*itr == threadId)
				break;
		}

		return itr;
	}

}