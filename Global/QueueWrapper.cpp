#include "QueueWrapper.h"

QueueWrapper::QueueManager* QueueWrapper::QueueManager::pQueueManagerInstance = nullptr;
std::mutex QueueWrapper::QueueManager::m_instance;

QueueWrapper::QueueManager::QueueManager()
{
	return;
}

QueueWrapper::QueueManager::~QueueManager()
{
	return;
}

void QueueWrapper::QueueManager::DeleteInstance()
{
	if (pQueueManagerInstance != nullptr)
	{
		delete pQueueManagerInstance;
		pQueueManagerInstance = nullptr;
	}
		
}

QueueWrapper::QueueManager* QueueWrapper::QueueManager::Instance()
{
	if (pQueueManagerInstance == nullptr)
	{
		std::lock_guard lock(m_instance); /* only 1 called actually creates the pointer */
		if(pQueueManagerInstance == nullptr)
			pQueueManagerInstance = new QueueManager();
	}
		

	return pQueueManagerInstance;
}