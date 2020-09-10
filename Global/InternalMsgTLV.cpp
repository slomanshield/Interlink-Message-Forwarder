#include "InternalMsgTLV.h"

InternalMsgTLV::InternalMsgTLV()
{
	Clear();
}

InternalMsgTLV::InternalMsgTLV(char* inputStream, uint32_t streamLength)
{
	SetDataFromTLV(inputStream, streamLength);
}
InternalMsgTLV::~InternalMsgTLV()
{
	return;
}
void InternalMsgTLV::Clear()
{
	test_data_id = "";
	padding = "";
	process_timeout = false;
	successful_process = false;
	time_stamp_create_nano = 0;
	reply_ips.clear();
}

void InternalMsgTLV::MoveString(char* in, std::string* out, LENGTH len)
{
	out->assign(in, len);
}

template<typename T> 
void InternalMsgTLV::MoveValue(T* in, T* out) /* should be used for simple type etc */
{
	*out = *in;
}

void InternalMsgTLV::ParseArray(char* in, void* out, LENGTH len, TYPE type)
{
	LENGTH lenRead = 0;
	LENGTH lenField = 0;

	for (lenRead = 0; lenRead < len; lenRead += lenField, in += lenField) /* increment over the field */
	{
		lenField = *(LENGTH*)in; /* get the length */
		in += sizeof(LENGTH); /* skip over the length field */
		lenRead += sizeof(LENGTH);
		MoveArrayValue(in, out, lenField, type);
	}
}

void InternalMsgTLV::MoveArrayValue(void* in,void* out,  LENGTH len, TYPE type)
{
	switch (type)
	{
		case TYPE_STRING:
		{
			std::list<std::string>* pList = (std::list<std::string>*)out;
			std::string tmp((char*)in, len);
			pList->push_back(tmp);
			break;
		}
		default:
			break;
	}
}

void InternalMsgTLV::AddTLVData(TAG tag, LENGTH len, char* data, std::string* ss)
{
	ss->append((char*)&tag, sizeof(TAG));
	ss->append((char*)&len, sizeof(LENGTH));
	ss->append((char*)data, len);
}

template<typename T>
void InternalMsgTLV::AddTLVArrayData(std::list<T>* inArray, TAG tag, TYPE type, std::string* ss)
{
	LENGTH len = 0;
	char* data = nullptr;
	std::string tmpArrayData = "";

	for (auto i = inArray->begin(); i != inArray->end(); i++)
	{
		switch (type)
		{
			case TYPE_STRING:
			{
				data = (char*)(*i).c_str();
				len = (*i).length();
				break;
			}

			default:
				break;
		}
		
		tmpArrayData.append((const char*)&len, sizeof(LENGTH));
		tmpArrayData.append((const char*)data, len);
	}

	len = tmpArrayData.length();
	if (len > 0)
	{
		ss->append((char*)&tag, sizeof(TAG));
		ss->append((char*)&len, sizeof(LENGTH));
		ss->append((char*)tmpArrayData.c_str(), tmpArrayData.length());
	}
}

void InternalMsgTLV::SetDataFromTLV(char* inputStream, uint32_t streamLength)
{
	std::string* pTmpStr = nullptr;
	LENGTH len = 0;
	LENGTH lenRead = 0;
	TAG tag = 0;
	Clear();

	for (lenRead = 0; lenRead < streamLength; inputStream += len, lenRead += len)/* interate over the data */
	{
		tag = *(TAG *)inputStream; /* get the tag */
		inputStream += sizeof(TAG); /* skip over the tag */
		lenRead += sizeof(TAG);
		len = *(LENGTH *)inputStream; /* get the length */
		inputStream += sizeof(LENGTH); /* skip over the length */
		lenRead += sizeof(LENGTH);

		/* we are now at the value lets see which tag it is */

		switch (tag) /* for complex data types only (not arrays) */
		{
			/* case for strings */
			case TAG_TEST_DATA_ID:
				pTmpStr = &test_data_id;
				break;
			case TAG_PADDING:
				pTmpStr = &padding;
				break;
			default:
				break;
		}

		switch (tag)
		{
			case TAG_TEST_DATA_ID:
			case TAG_PADDING:
				MoveString(inputStream, pTmpStr, len);
				break;
			case TAG_TIME_STAMP:
				MoveValue<uint64_t>((uint64_t*)inputStream, &time_stamp_create_nano);
				break;
			case TAG_PROCESS_TIMEOUT:
				MoveValue<bool>((bool*)inputStream, &process_timeout);
				break;
			case TAG_SUCCESS_PROCESS:
				MoveValue<bool>((bool*)inputStream, &successful_process);
				break;
			case TAG_REPLY_IPS:
				ParseArray(inputStream, (void*)&reply_ips, len, TYPE_STRING);
				break;
			default:
				break;
		}
		
	}
}



void InternalMsgTLV::GetTLVFromData(std::string* ss)
{
	ss->clear();

	AddTLVData(TAG_TEST_DATA_ID, test_data_id.length(), (char*)test_data_id.c_str(), ss);
	AddTLVData(TAG_PADDING, padding.length(), (char*)padding.c_str(), ss);
	AddTLVData(TAG_TIME_STAMP, sizeof(uint64_t), (char*)&time_stamp_create_nano, ss);
	AddTLVData(TAG_PROCESS_TIMEOUT, sizeof(bool), (char*)&process_timeout, ss);
	AddTLVData(TAG_SUCCESS_PROCESS, sizeof(bool), (char*)&successful_process, ss);
	AddTLVArrayData<std::string>(&reply_ips, TAG_REPLY_IPS, TYPE_STRING, ss);
}

std::string InternalMsgTLV::GetLastReplyIp(bool remove)
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
void InternalMsgTLV::AddReplyIp(std::string* in_reply_ip)
{
	reply_ips.push_back(*in_reply_ip);
}
bool InternalMsgTLV::GetProcessTimeout()
{
	return process_timeout;
}
void InternalMsgTLV::SetSuccessProcess(bool successful_process)
{
	this->successful_process = successful_process;
}
bool InternalMsgTLV::GetSucessProcssed()
{
	return successful_process;
}
void InternalMsgTLV::SetProcessTimeout(bool process_timeout)
{
	this->process_timeout = process_timeout;
}
std::string InternalMsgTLV::GetTestDataId()
{
	return test_data_id;
}
void InternalMsgTLV::SetTestDataId(std::string test_data_id)
{
	this->test_data_id = test_data_id;
}
void InternalMsgTLV::SetPadding(std::string* padding)
{
	this->padding = *padding;
}
void InternalMsgTLV::SetTimeStampCreate(uint64_t time_stamp_create_nano)
{
	this->time_stamp_create_nano = time_stamp_create_nano;
}
uint64_t InternalMsgTLV::GetTimeStampCreate()
{
	return time_stamp_create_nano;
}