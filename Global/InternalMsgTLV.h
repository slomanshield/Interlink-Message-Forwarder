#ifndef INTERNALMSGTLV_H
#define INTERNALMSGTLV_H

#include "global.h"

/* defines for TLV data */

#define TYPE_BOOL   0x0001
#define TYPE_UINT64 0x0002
#define TYPE_STRING 0x0003

#define TAG_TEST_DATA_ID	0x0001
#define TAG_PADDING			0x0002
#define TAG_TIME_STAMP		0x0003
#define TAG_PROCESS_TIMEOUT 0x0004
#define TAG_SUCCESS_PROCESS 0x0005
#define TAG_REPLY_IPS       0x0006

typedef uint32_t LENGTH;
typedef uint16_t TAG;
typedef uint16_t TYPE; /* used with arrays which go T(tag)L(length)V( L(length) V(value) ) nested data for arrays */

/***************************/



class InternalMsgTLV
{
public:
	InternalMsgTLV();
	~InternalMsgTLV();
	InternalMsgTLV(InternalMsgTLV& msgInternal);
	InternalMsgTLV(const InternalMsgTLV& msgInternal);
	void operator=(InternalMsgTLV&& msgInternal);
	InternalMsgTLV& operator=(const InternalMsgTLV& msgInternal);
	InternalMsgTLV(char* inputStream, uint32_t streamLength);
	void SetDataFromTLV(char* inputStream, uint32_t streamLength);
	void GetTLVFromData(std::string* ss);
	std::string GetLastReplyIp(bool remove);
	void AddReplyIp(std::string* in_reply_ip);
	bool GetProcessTimeout();
	void SetSuccessProcess(bool successful_process);
	bool GetSucessProcssed();
	void SetProcessTimeout(bool process_timeout);
	std::string GetTestDataId();
	void SetTestDataId(std::string test_data_id);
	void SetPadding(std::string* padding);
	void SetTimeStampCreate(uint64_t time_stamp_create_nano);
	uint64_t GetTimeStampCreate();
private:
	void Clear();
	void CopyFields(const InternalMsgTLV* pMsgTLV);
	void AddTLVData(TAG tag, LENGTH len, char* data, std::string* ss);
	template<typename T> void AddTLVArrayData(std::list<T>* inArray, TAG tag, TYPE type, std::string* ss);
	std::string test_data_id;
	std::string padding;
	uint64_t time_stamp_create_nano;
	bool process_timeout;
	bool successful_process;
	std::list<std::string> reply_ips;
	void MoveString(char* in, std::string* out, LENGTH len);
	template<typename T> void  MoveValue (T* in, T* out);
	void ParseArray(char* in, void* out, LENGTH len, TYPE type);
	void MoveArrayValue(void* in, void* out, LENGTH len, TYPE type);
};



#endif // !INTERNALMSGTLV_H
