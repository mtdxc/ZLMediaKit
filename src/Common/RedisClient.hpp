#pragma once
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>
// for va_list
#include <stdarg.h>
#include "Poller/Timer.h"
struct redisAsyncContext;
struct redisReply;

bool isReplyNull(const redisReply* r);
bool isReplyError(const redisReply* r);
int ReplyType(const redisReply* r);
int ReplyToInt(const redisReply* r);
typedef std::map<std::string, std::string> StrMap;
int ReplyToMap(const redisReply* r, StrMap& map);
typedef std::vector<std::string> StrVector;
int ReplyToVector(const redisReply* r, StrVector& map);
std::string ReplyToString(const redisReply* r);
// subscribe msg callback
typedef std::function<void(const std::string& msg)> MsgFunc;
// redis command callback
typedef std::function<void(redisReply*)> CmbFunc;

struct RedisCmd
{
	~RedisCmd();
	int idx;
	char* cmd = nullptr;
	int len   = 0;
	CmbFunc cb;
};

typedef std::function<void(int pubs, const char* ev)> RedisEvent;
class RedisClient
{
	RedisClient(const char* server, uint16_t port = 6379);
	~RedisClient();

	// redis server addr
	std::string server_;
	uint16_t port_;

	redisAsyncContext* pubs_ = nullptr;
	std::list<RedisCmd*> cmdList;

	redisAsyncContext* subs_ = nullptr;
	std::map<std::string, MsgFunc> subsMap;
	toolkit::Timer::Ptr subsChecker_  = 0;
	toolkit::Timer::Ptr connectTimer_ = 0;
	virtual void OnTimer(long timer);

	static void cmdCallback(redisAsyncContext* c, void* r, void* privdata);
	static void subsCallback(redisAsyncContext* c, void* r, void* privdata);
	MsgFunc getSubsCB(const std::string& channel);
	RedisEvent event_;
	void NotifyEvent(int handle, const char* event);

public:
	void SetEvent(RedisEvent e)
	{
		event_ = e;
	}
	static RedisClient* Instance();
	static void ClassInit(const std::string& host, uint16_t port);
	static void ClassDestory();
	void Close();
	void Dump();
	void onRedisConnected(const redisAsyncContext* ctx);
	void onRedisDisconnect(const redisAsyncContext* ctx);

	void command(CmbFunc cb, const char* fmt, ...)
	{
		va_list vl;
		va_start(vl, fmt);
		commandv(cb, fmt, vl);
		va_end(vl);
	}

	void command(const char* fmt, ...)
	{
		va_list vl;
		va_start(vl, fmt);
		commandv(nullptr, fmt, vl);
		va_end(vl);
	}
	void commandv(CmbFunc cb, const char* fmt, va_list vl);

	void publish(const std::string& channel, const std::string& msg);
	void subscribe(const std::string& channel, MsgFunc func);
	void unsubscribe(const std::string& channel);
};
