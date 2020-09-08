#include "BaseTimerEvent.h"

/* this implementation of POSIX timer uses threads, if your timer is needed in < 1ms please consider other options */

/* NOTE: if you go over the max of ulimit pending signals timer_create will return an error update /etc/security/limits.conf for RLIMIT_PENDING if you need more than 15k per process */

BaseTimerEvent::BaseTimerEvent()
{
	timerid = 0;
	memset(&se, 0, sizeof(struct sigevent));
	memset(&its, 0, sizeof(struct itimerspec));
	se.sigev_notify = SIGEV_THREAD; 
	se.sigev_value.sival_ptr = this;
	se.sigev_notify_function = BaseTimeEventHandler;
	se.sigev_notify_attributes = NULL;
}

BaseTimerEvent::~BaseTimerEvent()
{
	if(timerid != 0)
		timer_delete(timerid);
}

int BaseTimerEvent::Init()
{
	int cc = 0;

	if (timerid == 0) /* Check if timer is init or not */
	{
		cc = timer_create(CLOCK_MONOTONIC, &se, &timerid);

		if (cc != 0)
		{
			cc = errno;
			printf("Error BaseTimerEvent::Init() error code is %d \n", cc);
		}
	}
	
	return cc;
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