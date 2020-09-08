#ifndef OUTSTANDINGMESSAGES_H
#define OUTSTANDINGMESSAGES_H

namespace OustandingMessages
{
	using namespace QueueWrapper;
	enum { SUCCESS, MAX_OUTSTANDING_REACHED, BUCKET_NOT_FOUND, MESSAGE_NOT_FOUND,MESSAGE_ALREADY_EXISTS,REPLY_WAIT_TIMEOUT, TIMER_INIT_FAILED };
	template<typename T, typename msgType,int numBuckets>
	class OustandingMessageTracker
	{
	private:
		/* Start of timer class */

		class OutstandingTimer : public BaseTimerEvent
		{
		public:
			
			void TimeEventHandler()
			{
				DoubleMicro diff = std::chrono::duration_cast<std::chrono::microseconds>(HighResolutionTime::now() - writeTimeStamp);
				int cc = pQueueManager->PutDataOnQueue<msgType>(&timeoutQueueName, msgId);
				if (cc != QUEUE_SUCCESS)
					printf("OustandingMessageTracker failed to put timed out msgId on queue %s \n", timeoutQueueName.c_str());
				printf("Time After Write %lf \n", diff.count());
			}

			OutstandingTimer()
			{
				timeoutQueueName = "";
				pQueueManager = QueueManager::Instance();

			}
			~OutstandingTimer()
			{
				return;
			}

			void ArmTimer(uint64_t timeoutMilliseconds, std::string* timeoutQueueName, msgType msgId)
			{
				this->timeoutQueueName = *timeoutQueueName;
				this->msgId = msgId;
				its.it_value.tv_sec = timeoutMilliseconds / 1000;
				its.it_value.tv_nsec = (timeoutMilliseconds % 1000) * 1000000;
				timer_settime(timerid, 0, &its, NULL);
			}

		private:
			std::string timeoutQueueName;
			msgType msgId;
			QueueManager* pQueueManager;
		public:
			HighResolutionTimePointClock writeTimeStamp;
		};
	public:
	    struct MapBucketEntry
		{
			T userData;
			OutstandingTimer timerEvent;
		};
		typedef std::unordered_map<msgType, MapBucketEntry*> MapBucket;
		typedef typename std::unordered_map<msgType, MapBucketEntry*>::const_iterator MapBucketEntryItr;
		typedef typename std::list<MapBucketEntry*> MapBucketEntryList;
		typedef typename std::list<MapBucketEntry*>::const_iterator MapBucketEntryListItr;
		struct MapBucketContainer
		{
			std::mutex m;
			MapBucket mapBucket;
		};
		
		typedef typename std::unordered_map<int, MapBucketContainer*>::const_iterator mapOfMapsItr;
		OustandingMessageTracker()
		{
			for (int i = 0; i < numBuckets; i++)
			{
				mapOfMaps.insert({ i, new MapBucketContainer() });//init maps
			}
			numMaxOutStanding = 0;
			maxOustanding = 0;
		}
		~OustandingMessageTracker()
		{
			for (mapOfMapsItr itr = mapOfMaps.begin(); itr != mapOfMaps.end(); itr++)
			{
				delete (*itr).second;
			}
			for (MapBucketEntryListItr itrList = entryMemoryStorage.begin(); itrList != entryMemoryStorage.end(); itrList++)
			{
				delete *itrList;
			}
			return;
		}
		
		int InsertData(msgType msgId, T insertData, uint64_t messageTimeoutMilliseconds, std::string* timeoutQueueName)
		{
			int cc = SUCCESS;
			
			mapOfMapsItr itr = GetBucket(msgId);
			if (itr != mapOfMaps.end())
			{
				MapBucketContainer* pMapBucketContainer = (*itr).second;
				std::unique_lock<std::mutex> lk(pMapBucketContainer->m);
				MapBucketEntryItr entryItr = pMapBucketContainer->mapBucket.find(msgId);
				if (entryItr != pMapBucketContainer->mapBucket.end())
					cc = MESSAGE_ALREADY_EXISTS;
				else
				{
					MapBucketEntry* pEntry = GetFreeEntry();
					if (pEntry != nullptr)
					{
						pEntry->userData = insertData;
						pEntry->timerEvent.writeTimeStamp = HighResolutionTime::now();
						pEntry->timerEvent.ArmTimer(messageTimeoutMilliseconds, timeoutQueueName, msgId);
						pMapBucketContainer->mapBucket.insert({ msgId,pEntry });
					}
					else
						cc = MAX_OUTSTANDING_REACHED;
				}
				lk.unlock();
			}
			else
				cc = BUCKET_NOT_FOUND;

			return cc;
		}
		T GetData(msgType msgId, int* ccOut,bool remove)
		{
			mapOfMapsItr itr = GetBucket(msgId);
			T retData;
			*ccOut = SUCCESS;
			if (itr != mapOfMaps.end())
			{
				MapBucketContainer* pMapBucketContainer = (*itr).second;
				std::unique_lock<std::mutex> lk(pMapBucketContainer->m);
				MapBucketEntryItr entryItr = pMapBucketContainer->mapBucket.find(msgId);
				if (entryItr != pMapBucketContainer->mapBucket.end())
				{
					retData = ((*entryItr).second)->userData;
					if (remove == true)
					{
						((*entryItr).second)->timerEvent.DisarmTimer();
						PutFreeEntry((*entryItr).second);
						pMapBucketContainer->mapBucket.erase(entryItr);
					}
				}
				else
					*ccOut = MESSAGE_NOT_FOUND;
				lk.unlock();
			}
			else
				*ccOut = BUCKET_NOT_FOUND;

			return retData;
		}
		int RemoveData (msgType msgId)
		{
			int cc = SUCCESS;
			mapOfMapsItr itr = GetBucket(msgId);
			if (itr != mapOfMaps.end())
			{
				MapBucketContainer* pMapBucketContainer = (*itr).second;
				std::unique_lock<std::mutex> lk(pMapBucketContainer->m);
				MapBucketEntryItr entryItr = pMapBucketContainer->mapBucket.find(msgId);
				if (entryItr != pMapBucketContainer->mapBucket.end())
				{
					((*entryItr).second)->timerEvent.DisarmTimer();
					PutFreeEntry((*entryItr).second);
					pMapBucketContainer->mapBucket.erase(entryItr);
				}
				else
					cc = MESSAGE_NOT_FOUND;
				lk.unlock();
			}
			else
				cc = BUCKET_NOT_FOUND;


			return cc;
		}
		
		uint64_t GetNumOutStandingMessages()
		{
			return numMaxOutStanding;
		}
		void SetMaxOutStanding(uint64_t maxOustanding)
		{
			if (maxOustanding > entryMemoryStorage.size())
			{
				uint64_t sizeList = 0;
				std::lock_guard<std::mutex> lock(mutexMaxOutStanding);
				sizeList = entryMemoryStorage.size();
				for (uint64_t i = 0; i < maxOustanding - sizeList; i++)
				{
					MapBucketEntry* pTemp = new MapBucketEntry;
					pTemp->timerEvent.Init(); /* For now just assume it will work */
					entryFreeList.push_back(pTemp);
					entryMemoryStorage.push_back(pTemp);
				}
			}
			this->maxOustanding = maxOustanding;
		}
		int WaitForOutStandingMessages(double waitTimeMilliSeconds)
		{
			int cc = SUCCESS;
			HighResolutionTimePointClock startTime = HighResolutionTime::now();
			HighResolutionTimePointClock currentTime = startTime;
			DoubleMili diff;
			
			while (numMaxOutStanding > 0 && cc == SUCCESS)
			{
				usleep(1000); /* sleep for 1 milli second to give up CPU */
				currentTime = HighResolutionTime::now();
				diff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
				if (diff.count() > waitTimeMilliSeconds)
					cc = REPLY_WAIT_TIMEOUT;
			}

			return cc;
		}
	private:
		mapOfMapsItr GetBucket(msgType msgId)
		{
			int numBucket = std::hash<msgType>{}(msgId) % numBuckets;
			mapOfMapsItr itr = mapOfMaps.find(numBucket);
			return itr;
		}
		void PutFreeEntry(MapBucketEntry* pFreeEntry)
		{
			std::lock_guard<std::mutex> lock(mutexMaxOutStanding);
			entryFreeList.push_back(pFreeEntry);
			numMaxOutStanding--;
			return;
		}
		MapBucketEntry* GetFreeEntry()
		{
			std::lock_guard<std::mutex> lock(mutexMaxOutStanding);
			if (numMaxOutStanding >= maxOustanding)
				return nullptr;
			else
			{
				MapBucketEntry* pTemp = entryFreeList.front();
				entryFreeList.pop_front();
				numMaxOutStanding++;
				return pTemp;
			}
		}
	private:
		std::unordered_map<int, MapBucketContainer*> mapOfMaps;
		uint64_t  numMaxOutStanding;
		uint64_t  maxOustanding;
		std::mutex mutexMaxOutStanding;
		MapBucketEntryList entryFreeList;
		MapBucketEntryList entryMemoryStorage;
	};

	
}

#endif // !OUTSTANDINGMESSAGES_H