#ifndef THREAD_WRAPPER_H
#define THREAD_WRAPPER_H


#include <thread>
#include <mutex>
#include <list>
#include <chrono>
#include <ratio>
#include <iostream>
#include <condition_variable>
using namespace std;

typedef std::chrono::high_resolution_clock HighResolutionTime;
typedef std::chrono::time_point<std::chrono::_V2::system_clock, std::chrono::nanoseconds> HighResolutionTimePointClock;
typedef std::chrono::duration<double, std::milli> DoubleMili;
typedef std::chrono::duration<double, std::micro> DoubleMicro;

typedef  void (threadProcess)(bool* running, bool* stopped, void* usrPtr);

typedef struct
{
	bool* isRunning;
	std::thread* th;
}ThreadData;


template<typename T>
class  ThreadWrapper {


public:
	ThreadWrapper();
	~ThreadWrapper();
	bool StopProcessing(double timeoutMilliseconds);
	void SetProcessthread(T*);
private:
	std::list <ThreadData> isRunningThreadList;
	std::list <ThreadData> orphanedThreadList;
	T* processthread;
	bool continueProcessing;
	int numThreads;
public:
	template<typename... Targs> bool StartProcessing(int numThreads, void* usrPtr, Targs...Fargs)//must be inline or will not compile
	{
		continueProcessing = true;
		ThreadData tmpThreadData;
		bool* tmpBool = nullptr;
		std::thread* tmpTh = nullptr;
		if (this->numThreads == 0 && this->processthread != nullptr)
		{
			for (int i = 0; i < numThreads; i++)
			{
				tmpBool = new bool;
				*tmpBool = false;
				tmpTh = new std::thread([=] { processthread(&continueProcessing, tmpBool, usrPtr, Fargs...); });
				tmpTh->detach();
				tmpThreadData.isRunning = tmpBool;
				tmpThreadData.th = tmpTh;
				isRunningThreadList.push_back(tmpThreadData);
			}
			this->numThreads = numThreads;
			return true;
		}
		else
			return false;
	}

};

template<typename T> ThreadWrapper<T>::~ThreadWrapper()
{
}

template<typename T> ThreadWrapper<T>::ThreadWrapper()
{
	numThreads = 0;
	this->processthread = nullptr;
}
template<typename T> void ThreadWrapper<T>::SetProcessthread(T* processthread)
{
	this->processthread = processthread;
}

template<typename T>
bool ThreadWrapper<T>::StopProcessing(double timeoutMilliSeconds)
{
	continueProcessing = false;
	bool allStopped = false;
	bool timeOut = false;
	HighResolutionTimePointClock startTime = HighResolutionTime::now();
	HighResolutionTimePointClock currentTime = startTime;
	DoubleMili diff;
	std::list <ThreadData>::iterator it;
	bool** tmp;


	while (!allStopped && !timeOut)
	{
		it = isRunningThreadList.begin();
		for (; it != isRunningThreadList.end(); it++)
		{
			tmp = &(*it).isRunning;
			if (*tmp != nullptr && !**tmp)
				break;
			else if (*tmp != nullptr && **tmp)
			{
				delete *tmp;
				*tmp = nullptr;
				delete (*it).th;
				(*it).th = nullptr;
			}
		}
		if (it == isRunningThreadList.end())
			allStopped = true;

		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		if (timeoutMilliSeconds > 0 && allStopped == false)
		{
			currentTime = HighResolutionTime::now();
			diff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
			if (diff.count() > timeoutMilliSeconds)
				timeOut = true;
		}
	}
	

	if (allStopped)
	{
		numThreads = 0;
		isRunningThreadList.clear();
	}

	
	return allStopped;
}

#endif // !THREAD_WRAPPER_H