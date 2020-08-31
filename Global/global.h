#ifndef GLOBAL_H
#define GLOBAL_H

#include <cstdio>
#include <string.h>
#include <stdlib.h>
#include <memory>
#include <queue>
#include <iostream>
#include <ctime>
#include <ratio>
#include <chrono>
#include <mutex>
#include <time.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <uuid/uuid.h>
#include <sstream>
#include <errno.h>
#include <unistd.h>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "ThreadWrapper.h"
#include "QueueWrapper.h"
#include "BaseTimerEvent.h"
#include "OutstandingMessageTracker.h"


using namespace std;
using namespace QueueWrapper;
using namespace rapidjson;

#define FOR_NULL 1
#define MAX_HOSTNAME_LEN 256
#define THREAD_WAIT_TIMEOUT 5000

static bool TerminateApplication = false;

static struct sigaction new_action, old_action;

template<typename ... Args>
 static std::string string_format(const std::string& format, Args ... args)
{
	size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	char* buffer = new char[size];
	std::string out;
	memset(buffer, 0, size);
	snprintf(buffer, size, format.c_str(), args ...);
	out = buffer;
	delete[] buffer;
	return out;
}

static void GetJsonString(Document* doc, std::string* out)
{
	rapidjson::StringBuffer buffer;

	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc->Accept(writer);

	out->append(buffer.GetString());

}

class StringStreamWrapper
{
public:
	std::stringstream s = std::stringstream("",std::ios::app | std::ios::binary | std::ios::in | std::ios::out);
	StringStreamWrapper() { s.str(""); };
	StringStreamWrapper(const StringStreamWrapper& str_wrapper)
	{
		this->s << str_wrapper.s.rdbuf();
	}

	StringStreamWrapper(StringStreamWrapper& str_wrapper)
	{
		this->s << str_wrapper.s.rdbuf();
		str_wrapper.s.seekg(0, str_wrapper.s.beg);
	}
	~StringStreamWrapper() {};

	inline StringStreamWrapper& operator=(StringStreamWrapper& str_wrapper)
	{
		this->Clear();
		this->s << str_wrapper.s.rdbuf();
		return *this;
	}

	inline void operator=(StringStreamWrapper&& str_wrapper)
	{
		this->Clear();
		this->s << str_wrapper.s.rdbuf();
	}

	inline uint64_t  Read(char* in, uint64_t length)
	{
		uint64_t num_bytes_read = 0;

		if (this->s.tellg() > 0)/* set to 0 if not at start */
			this->s.seekg(0, this->s.beg);

		this->s.read(in, length);

		num_bytes_read = this->s.gcount();

		this->s.seekg(0, this->s.beg);

		return num_bytes_read;
	}

	inline void Clear()
	{
		this->s.str("");
		this->s.clear();
	}

	inline void Write(char* in, uint64_t length)
	{
		Clear();
		this->s.write(in, length);
	}

	inline void Append(char* in, uint64_t length)
	{
		this->s.write(in, length);
	}

	inline uint64_t Length()
	{
		uint64_t length = 0;
		this->s.seekg(0, this->s.end); /* go to the end */
		length = this->s.tellg(); /* get position */
		this->s.seekg(0, this->s.beg); /* go back to start */
		return length;
	}
};


/* TODO: this is a testing class should be removed when used */
class TierMessageInternal
{
public:
	TierMessageInternal()
	{
		Clear();
	}
	TierMessageInternal(std::string strInput)
	{
		SetDataFromJson(strInput);
	}
	~TierMessageInternal()
	{
		return;
	}
	void Clear()
	{
		test_data_id = "";
		padding = "";
		process_timeout = false;
		successful_process = false;
		time_stamp_create_nano = 0;
		reply_ips.clear();
	}
	std::string GetJsonData()
	{
		Value replyArray;
		Value replyIdValue;
		Value create_time_stamp;
		Value testDataId;
		Value paddingStr;
		Document doc;
		std::string str;
		doc.SetObject();
		/* without passing doc allocator only pointer is used data is not copied for SetString */
		testDataId.SetString(test_data_id.c_str(), test_data_id.length());
		paddingStr.SetString(padding.c_str(), padding.length());
		doc.AddMember("test_data_id", testDataId, doc.GetAllocator());
		doc.AddMember("padding", paddingStr, doc.GetAllocator());
		doc.AddMember("process_timeout", process_timeout, doc.GetAllocator());
		doc.AddMember("successful_process", successful_process, doc.GetAllocator());
		create_time_stamp.SetUint64(time_stamp_create_nano);
		doc.AddMember("time_stamp_create_nano", create_time_stamp, doc.GetAllocator());
		replyArray.SetArray();
		for (std::list<std::string>::iterator itr = reply_ips.begin(); itr != reply_ips.end(); itr++)
		{
			replyIdValue.SetString((*itr).c_str(),(*itr).length());
			replyArray.PushBack(replyIdValue, doc.GetAllocator());
		}
		doc.AddMember("reply_ips", replyArray, doc.GetAllocator());
		GetJsonString(&doc, &str);
		return str;
	}
	void SetDataFromJson(std::string strInput)
	{
		Document doc;
		Value arr;
		doc.Parse(strInput.c_str(), strInput.length());
		Clear();
		if (!doc.HasParseError())//for now assume data is correct
		{
			test_data_id = doc["test_data_id"].GetString();
			padding = doc["padding"].GetString();
			process_timeout = doc["process_timeout"].GetBool();
			successful_process = doc["successful_process"].GetBool();
			time_stamp_create_nano = doc["time_stamp_create_nano"].GetUint64();
			arr = doc["reply_ips"].GetArray();
			for (Value* arrayValue = arr.Begin(); arrayValue != arr.End(); arrayValue++)
			{
				reply_ips.push_back(arrayValue->GetString());
			}
		}
		else
			printf("Error parsing %s \n", strInput.c_str());
	}
	std::string GetLastReplyIp(bool remove)
	{
		std::string reply_ip = "";
		if (reply_ips.size() > 0)
		{
			reply_ip = reply_ips.back();
			if (remove)
				reply_ips.pop_back();
		}
		return reply_ip;
	}
	void AddReplyIp(std::string* in_reply_ip)
	{
		reply_ips.push_back(*in_reply_ip);
	}
	bool GetProcessTimeout()
	{
		return process_timeout;
	}
	void SetSuccessProcess(bool successful_process)
	{
		this->successful_process = successful_process;
	}
	bool GetSucessProcssed()
	{
		return successful_process;
	}
	void SetProcessTimeout(bool process_timeout)
	{
		this->process_timeout = process_timeout;
	}
	std::string GetTestDataId()
	{
		return test_data_id;
	}
	void SetTestDataId(std::string test_data_id)
	{
		this->test_data_id = test_data_id;
	}
	void SetPadding(std::string padding)
	{
		this->padding = padding;
	}
	void SetTimeStampCreate(uint64_t time_stamp_create_nano)
	{
		this->time_stamp_create_nano = time_stamp_create_nano;
	}
	uint64_t GetTimeStampCreate()
	{
		return time_stamp_create_nano;
	}
private:
	std::string test_data_id;
	std::string padding;
	uint64_t time_stamp_create_nano;
	bool process_timeout;
	bool successful_process;
	std::list<std::string> reply_ips;
};


static void sig_term_handler(int signum)
{
	if (signum == SIGTERM || signum == SIGQUIT || signum == SIGKILL || signum == SIGINT)
		TerminateApplication = true;
}

static void SetTermHandler()
{
	new_action.sa_handler = sig_term_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	sigaction(SIGTERM, &new_action, NULL);

	sigaction(SIGQUIT, &new_action, NULL);

	sigaction(SIGKILL, &new_action, NULL);
}

static int RegisterQueueReadThread(std::string queueName)
{
	std::thread::id threadId = std::this_thread::get_id();
	QueueWrapper::QueueManager* pQueueManager = QueueWrapper::QueueManager::Instance();
	int cc = pQueueManager->RegisterThreadToQueue<StringStreamWrapper>(&queueName, threadId);
	return cc;
}

static int RemoveQueueReadThread(std::string queueName)
{
	std::thread::id threadId = std::this_thread::get_id();
	QueueWrapper::QueueManager* pQueueManager = QueueWrapper::QueueManager::Instance();
	int cc = pQueueManager->RemoveThreadFromQueue<StringStreamWrapper>(&queueName, threadId);
	return cc;
}

static void SleepOnQueue(int cc)
{
	if (cc == QUEUE_NOT_FOUND)//sleep for 10 milli if no queue avaiable so we dont spin
		usleep(10000);
}

static std::string GetUuid()
{
	char uuid_str[UUID_STR_LEN];
	uuid_t uuid;
	uuid_generate_random(uuid);
	uuid_unparse(uuid, uuid_str);
	return std::string(uuid_str);
}

static std::string GetHexString(char* buffer, uint64_t length)
{
	std::string hex_str;
	char printfBuffer[10] = { 0 };

	for (uint64_t i = 0; i < length; i++)
	{
		sprintf(printfBuffer, "0x%02X ", buffer[i]);
		hex_str.append(printfBuffer);
	}

	return hex_str;
}

#endif