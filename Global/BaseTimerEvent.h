#ifndef BASETIMEREVENT_H
#define BASETIMEREVENT_H

#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <iostream>
#include <mutex>
#include <vector>
#include <sys/time.h>
#include <sys/resource.h>
#include "ThreadWrapper.h"

class BaseTimerEvent //TODO implement static routine to set RLIMIT for pending signals
{
public:
	BaseTimerEvent();

	~BaseTimerEvent();

	static void BaseTimeEventHandler(union sigval arg);

	static int IncreaseSignalLimit(rlim_t limit);

	virtual void TimeEventHandler();

	void DisarmTimer();

protected:
	timer_t timerid;
	struct sigevent se;
	struct itimerspec its;

};

#endif // !BASETIMEREVENT_H
