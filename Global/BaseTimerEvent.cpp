#include "BaseTimerEvent.h"

/* this implementation of POSIX timer uses threads, if your timer is needed in < 1ms please consider other options */

BaseTimerEvent::BaseTimerEvent()
{
	timerid = 0;
	memset(&se, 0, sizeof(struct sigevent));
	memset(&its, 0, sizeof(struct itimerspec));
	se.sigev_notify = SIGEV_THREAD; 
	se.sigev_value.sival_ptr = this;
	se.sigev_notify_function = BaseTimeEventHandler;
	se.sigev_notify_attributes = NULL;
	timer_create(CLOCK_MONOTONIC, &se, &timerid);
}

BaseTimerEvent::~BaseTimerEvent()
{
	timer_delete(timerid);
}

void BaseTimerEvent::BaseTimeEventHandler(union sigval si)
{
	BaseTimerEvent* pBaseTimerEvent = (BaseTimerEvent*)si.sival_ptr;
	pBaseTimerEvent->TimeEventHandler();
}

void BaseTimerEvent::DisarmTimer()
{
	memset(&its, 0, sizeof(struct itimerspec));
	timer_settime(timerid, 0, &its, NULL);
}

void BaseTimerEvent::TimeEventHandler()
{
	printf("BaseTimerEvent::TimeEventHandler Timer Hit!  \n");
}