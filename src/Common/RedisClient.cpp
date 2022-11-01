#define MS_CLASS "RedisClient"

#ifdef _WIN32
#define _SSIZE_T_
#define strcasecmp stricmp
#endif

extern "C" {
#include "hiredis.h"
#include "async.h"
}
#include "Util/logger.h"
#include "RedisClient.hpp"
#include <algorithm>
#include "Poller/EventPoller.h"
#include "Network/Socket.h"
using namespace toolkit;
class redisToolkitEvents {
  redisAsyncContext* _context;
  EventPoller::Ptr _poller;
  int fd = 0;
  std::weak_ptr<EventPoller::DelayTask> _timer;
public:
  int event = 0;
  redisToolkitEvents(EventPoller::Ptr loop, redisAsyncContext* ac) : _poller(loop), _context(ac) {
    fd = ac->c.fd;
  }

  void cleanup() {
    if (auto timer = _timer.lock()) {
      timer->cancel();
    }
	auto pool = _poller;
	auto pThis =  this;
	_poller = nullptr;
	if (event) {
      pool->delEvent(fd, [pThis](bool success){
		delete pThis;
	  });
      event = 0;
	}
	else{
		delete pThis;
	}
  }
  void updateEvent(int ev);
  void setTimeOut(uint64_t millis);
};

void redisToolkitEvents::updateEvent(int ev)
{
  if (ev == event || !_poller) return;
  if (!ev) {
    _poller->delEvent(fd);
    event = 0;
    return;
  }

  if (event)
    _poller->modifyEvent(fd, ev);
  else {
    redisAsyncContext* context = _context;
    _poller->addEvent(fd, ev, [context](int event) {
	  if (event & EventPoller::Event_Error) {
		PrintW("redis error %p", context);
	  }
      if (event & EventPoller::Event_Read) {
        redisAsyncHandleRead(context);
      }
      if (event & EventPoller::Event_Write) {
        redisAsyncHandleWrite(context);
      }
    });
  }
  event = ev;
}

void redisToolkitEvents::setTimeOut(uint64_t millis)
{
  if (auto timer = _timer.lock()) {
    timer->cancel();
  }
  if (millis == 0) {
    /* Libhv disallows zero'd timers so treat this as a delete or NO OP */
  }
  else if(_poller) {
    redisAsyncContext* context = _context;
    _timer = _poller->doDelayTask(millis, [context]() {
      redisAsyncHandleTimeout(context);
      return 0;
    });
  }
}

static void redisToolkitAddRead(void *privdata) {
  redisToolkitEvents* events = (redisToolkitEvents*)privdata;
  events->updateEvent(events->event | EventPoller::Event_Read);  
}

static void redisToolkitDelRead(void *privdata) {
  redisToolkitEvents* events = (redisToolkitEvents*)privdata;
  events->updateEvent(events->event & ~EventPoller::Event_Read);
}

static void redisToolkitAddWrite(void *privdata) {
  redisToolkitEvents* events = (redisToolkitEvents*)privdata;
  events->updateEvent(events->event | EventPoller::Event_Write);
}

static void redisToolkitDelWrite(void *privdata) {
  redisToolkitEvents* events = (redisToolkitEvents*)privdata;
  events->updateEvent(events->event & ~EventPoller::Event_Write);
}

static void redisToolkitCleanup(void *privdata) {
  redisToolkitEvents* events = (redisToolkitEvents*)privdata;
  events->cleanup();
  // delete events;
}

static void redisToolkitSetTimeout(void *privdata, struct timeval tv) {
  redisToolkitEvents* events = (redisToolkitEvents*)privdata;
  uint32_t millis = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  events->setTimeOut(millis);
}

static int redisToolkitAttach(redisAsyncContext* ac, EventPoller::Ptr loop) {
  redisContext *c = &(ac->c);

  if (ac->ev.data != NULL) {
    return REDIS_ERR;
  }
  redisToolkitEvents *events = new redisToolkitEvents(loop, ac);
  if (events == NULL) {
    return REDIS_ERR;
  }

  ac->ev.addRead = redisToolkitAddRead;
  ac->ev.delRead = redisToolkitDelRead;
  ac->ev.addWrite = redisToolkitAddWrite;
  ac->ev.delWrite = redisToolkitDelWrite;
  ac->ev.cleanup = redisToolkitCleanup;
  ac->ev.scheduleTimer = redisToolkitSetTimeout;
  ac->ev.data = events;

  return REDIS_OK;
}

inline bool isReplyNull(const redisReply* r)
{
	return !r || r->type == REDIS_REPLY_NIL;
}

inline bool isReplyError(const redisReply* r)
{
	return r && r->type == REDIS_REPLY_ERROR;
}

inline int ReplyType(const redisReply* r)
{
	if (!r) return REDIS_REPLY_NIL;
	return r->type;
}

int ReplyToInt(const redisReply* r)
{
	// REDIS_REPLY_INTEGER
	// REDIS_REPLY_BOOL
	return r && r->integer;
}

int ReplyToMap(const redisReply* r, StrMap& map)
{
	if (r && r->type == REDIS_REPLY_ARRAY)
	{
		map.clear();
		size_t count = r->elements;
		if (count % 2)
			count--;
		for (size_t i = 0; i < count; i += 2)
		{
			map[r->element[i]->str] = ReplyToString(r->element[i + 1]);
		}
		return map.size();
	}
	return -1;
}

int ReplyToVector(const redisReply* r, StrVector& vec)
{
	if (r && r->type == REDIS_REPLY_ARRAY)
	{// REDIS_REPLY_MAP REDIS_REPLY_SET REDIS_REPLY_PUSH
		vec.clear();
		for (size_t i = 0; i < r->elements; i++)
		{
			vec.push_back(ReplyToString(r->element[i]));
		}
		return vec.size();
	}
	return -1;
}

std::string ReplyToString(const redisReply* r)
{
	std::string ret;
	if (r==nullptr) return ret;
	switch (r->type)
	{
	case REDIS_REPLY_ERROR:
	case REDIS_REPLY_STATUS:
	case REDIS_REPLY_VERB:
	case REDIS_REPLY_STRING:
		ret.assign(r->str, r->len);
		break;
	case REDIS_REPLY_DOUBLE:
		ret = std::to_string(r->dval);
		break;
	case REDIS_REPLY_INTEGER:
	case REDIS_REPLY_BOOL:
		ret = std::to_string(r->integer);
		break;
	}
	return ret;
}

void getCallback(redisAsyncContext* c, void* r, void* privdata)
{
	redisReply* reply = (redisReply*)r;
	if (reply == NULL)
		return;
	int cmd_idx = (long)r;
	switch (reply->type)
	{
		case REDIS_REPLY_NIL:
			PrintD("redis %d reply nil", cmd_idx);
			break;
		case REDIS_REPLY_VERB:
		case REDIS_REPLY_STATUS:
		case REDIS_REPLY_STRING:
			PrintD("redis %d reply %s", cmd_idx, reply->str);
			break;
		case REDIS_REPLY_ERROR:
			PrintD("redis %d error %s", cmd_idx, reply->str);
			break;
		case REDIS_REPLY_BOOL:
			PrintD("redis %d reply %s", cmd_idx, reply->integer?"true":"false");
			break;
		case REDIS_REPLY_INTEGER:
			PrintD("redis %d reply %lld", cmd_idx, reply->integer);
			break;
		case REDIS_REPLY_DOUBLE:
			PrintD("redis %d reply %f", cmd_idx, reply->dval);
			break;
		case REDIS_REPLY_ARRAY:
			PrintD("redis %d reply %d array", cmd_idx, reply->elements);
			break;
		case REDIS_REPLY_SET:
			PrintD("redis %d reply %d set", cmd_idx, reply->elements);
			break;
		case REDIS_REPLY_MAP:
			PrintD("redis %d reply %d map", cmd_idx, reply->elements);
			break;
		default:
			PrintD("redis %d reply type %d", cmd_idx, reply->type);
			break;
	}
}

RedisCmd::~RedisCmd()
{
	if (cmd)
	{
		redisFreeCommand(cmd);
		cmd = nullptr;
	}
	len = 0;
}

void connectCallback(const redisAsyncContext* c, int status)
{
	auto pMgr = RedisClient::Instance();
	if (!pMgr)
		return;
	if (status != REDIS_OK)
	{
		PrintW("connect Error: %s", c->errstr);
		pMgr->onRedisDisconnect(c);
	}
	else
	{
		PrintD("Connected...");
		pMgr->onRedisConnected(c);
	}
}

void disconnectCallback(const redisAsyncContext* c, int status)
{
	const char* err = (c && c->errstr) ? c->errstr : "";
	PrintW("Disconnected %d(%s)...", status, err);
	if (status == REDIS_OK)
	{
		return;
	}
	if (auto cli = RedisClient::Instance())
		cli->onRedisDisconnect(c);
}

static EventPoller::Ptr getLoop() {
  return EventPollerPool::Instance().getPoller();
}

redisAsyncContext* asyncConnect(const char* server, uint16_t port)
{
	redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, server, port);
	static timeval tv = {10, 0};
	options.connect_timeout = &tv;
	redisAsyncContext* c = redisAsyncConnectWithOptions(&options);
	if (c->err)
	{
		/* Let *c leak for now... */
		PrintW("redisAsyncConnect Error: %s", c->errstr);
		disconnectCallback(nullptr, -1);
		return nullptr;
	}
	PrintD("try to connect redis %s:%d", server, port);
	// redisLibuvAttach(c, DepLibUV::GetLoop());
	redisToolkitAttach(c, getLoop());
	redisAsyncSetConnectCallback(c, connectCallback);
	redisAsyncSetDisconnectCallback(c, disconnectCallback);
	// redisAsyncCommand(c, getCallback, (char*)"end-1", "GET key");
	return c;
}

static RedisClient* cli = nullptr;
RedisClient* RedisClient::Instance()
{
	return cli;
}

void RedisClient::ClassInit(const std::string& host, uint16_t port)
{
	if (!cli && host.length())
	{
		cli = new RedisClient(host.c_str(), port);
	}
}

void RedisClient::ClassDestory()
{
	if (cli)
	{
		delete cli;
		cli = nullptr;
	}
}

RedisClient::RedisClient(const char* server, uint16_t port /*= 6379*/)
{
	port_ = port;
	if (server && server[0])
	{
		server_ = server;
		asyncConnect(server, port);
		asyncConnect(server, port);
	}
}

RedisClient::~RedisClient()
{
	/* Disconnect after receiving the reply to GET */
	server_.clear();
	Close();
}

void RedisClient::Close()
{
	/*
	for (auto cmd : cmdList){
	  delete cmd;
	}
	*/
	cmdList.clear();
	if (pubs_)
	{
		redisAsyncDisconnect(pubs_);
		pubs_ = nullptr;
	}

	subsMap.clear();
	if (subs_)
	{
		redisAsyncDisconnect(subs_);
		subs_ = nullptr;
	}

	if (connectTimer_)
	{
		connectTimer_ = 0;
	}
	if (subsChecker_)
	{
		subsChecker_ = 0;
	}
}

MsgFunc RedisClient::getSubsCB(const std::string& channel)
{
	MsgFunc ret;
	if (subsMap.count(channel))
		ret = subsMap[channel];
	return ret;
}

void RedisClient::NotifyEvent(int handle, const char* event)
{
	PrintD("redis %s %s", handle ? "pubs" : "subs", event);
	if (event_)
		event_(handle, event);
}

void RedisClient::OnTimer(long timer)
{
	if (server_.empty())
		return;

	if (pubs_ == nullptr)
	{
		NotifyEvent(1, "reconnecting");
		asyncConnect(server_.c_str(), port_);
	}

	if (subsMap.size())
	{
		if (subs_ == nullptr)
		{
			NotifyEvent(0, "reconnecting");
			asyncConnect(server_.c_str(), port_);
		}
		else if (timer && subsMap.size())
		{
#if 0
			std::string channel = subsMap.begin()->first.c_str();
			MS_DEBUG("subs keepalive with channel %s", channel.c_str());
			redisAsyncCommand(subs_, &subsCallback, nullptr, "subscribe %s", channel.c_str());
#else
			PrintD("subs keepalive with ping");
			redisAsyncCommand(subs_, &getCallback, nullptr, "ping");
#endif
		}
	}
}

void RedisClient::onRedisDisconnect(const redisAsyncContext* ctx)
{
	if (pubs_ == ctx)
	{
		NotifyEvent(1, "end");
		pubs_ = nullptr;
	}
	else if (subs_ == ctx)
	{
		NotifyEvent(0, "end");
		subs_ = nullptr;
	}
	if (server_.length())
	{
		connectTimer_ = std::make_shared<Timer>(1.0, [this]() {
			OnTimer(0);
			return false;
		}, getLoop());
	}
}

void RedisClient::onRedisConnected(const redisAsyncContext* ctx)
{
	if (!pubs_)
	{
		pubs_ = (redisAsyncContext*)ctx;
		NotifyEvent(1, "connect");
		PrintD("pubs connected, %d pending cmd", cmdList.size());
		// 重新执行命令
		for (auto cmd : cmdList)
		{
			redisAsyncFormattedCommand(pubs_, &cmdCallback, cmd, cmd->cmd, cmd->len);
		}
		NotifyEvent(1, "ready");
	}
	else if (!subs_)
	{
		subs_ = (redisAsyncContext*)ctx;
		NotifyEvent(0, "connect");
		PrintD("subs connected, %d pending topic", subsMap.size());
		// 重新执行订阅
		for (auto it : subsMap)
		{
			redisAsyncCommand(subs_, &subsCallback, nullptr, "subscribe %s", it.first.c_str());
		}
		NotifyEvent(0, "ready");
	}
}

void RedisClient::cmdCallback(redisAsyncContext* c, void* r, void* privdata)
{
	redisReply* reply = (redisReply*)r;
	RedisCmd* cmd     = (RedisCmd*)privdata;
	if (cmd)
	{
		int cmd_idx = cmd->idx;
		if (reply)
		{
			switch (reply->type)
			{
				case REDIS_REPLY_NIL:
					PrintD("cmd %d reply nil", cmd_idx);
					break;
				case REDIS_REPLY_VERB:
				case REDIS_REPLY_STATUS:
				case REDIS_REPLY_STRING:
					PrintD("cmd %d reply %s", cmd_idx, reply->str);
					break;
				case REDIS_REPLY_ERROR:
					PrintD("cmd %d error %s", cmd_idx, reply->str);
					break;
				case REDIS_REPLY_BOOL:
					PrintD("cmd %d reply %s", cmd_idx, reply->integer?"true":"false");
					break;
				case REDIS_REPLY_INTEGER:
					PrintD("cmd %d reply %lld", cmd_idx, reply->integer);
					break;
				case REDIS_REPLY_DOUBLE:
					PrintD("cmd %d reply %f", cmd_idx, reply->dval);
					break;
				case REDIS_REPLY_ARRAY:
					PrintD("cmd %d reply %d array", cmd_idx, reply->elements);
					break;
				case REDIS_REPLY_SET:
					PrintD("cmd %d reply %d set", cmd_idx, reply->elements);
					break;
				case REDIS_REPLY_MAP:
					PrintD("cmd %d reply %d map", cmd_idx, reply->elements);
					break;
				default:
					PrintD("cmd %d reply type %d", cmd_idx, reply->type);
					break;
			}
		}
		else
		{
			PrintD("cmd %d reply nullptr", cmd_idx);
		}

		if (cmd->cb)
			cmd->cb(reply);
		if (auto pMgr = RedisClient::Instance())
		{
			auto& lst = pMgr->cmdList;
			auto it   = std::find(lst.begin(), lst.end(), cmd);
			if (it != lst.end())
				lst.erase(it);
		}
		delete cmd;
	}
}

static int cmdIdx = 0;
static int subsCount = 0;
static int lastSubs = 0;
static int lastCmd = 0;
static uint64_t lastTick = 0;
void RedisClient::Dump()
{
	if (lastCmd != cmdIdx || lastSubs!=subsCount) {
		uint64_t now = getCurrentMillisecond();
		int deltaCmd = cmdIdx - lastCmd;
		int deltaSubs = subsCount - lastSubs;
		int64_t tmDelta = now - lastTick;
		PrintD("%d cmds executes, %d pending, %5.2f cmds/s; %d subs receive,subs %d topics, %5.2f subs/s", 
			deltaCmd , cmdList.size(), deltaCmd * 1000.0 / tmDelta, 
			deltaSubs, subsMap.size(), deltaSubs* 1000.0 / tmDelta);
		lastCmd = cmdIdx;
		lastSubs = subsCount;
		lastTick = now;
	}
}


void RedisClient::commandv(CmbFunc cb, const char* fmt, va_list vl)
{
	if (server_.empty())
		return;
	RedisCmd* cmd     = new RedisCmd();
	cmd->idx          = ++cmdIdx;
	cmd->cb           = cb;
	cmd->len          = redisvFormatCommand(&cmd->cmd, fmt, vl);
	// 只有等到回应时才删除Cmd, 保证肯定会得到执行
	cmdList.push_back(cmd);
	PrintD("cmd %d: %s", cmdIdx, fmt);
	if (pubs_)
	{
		redisAsyncFormattedCommand(pubs_, &cmdCallback, cmd, cmd->cmd, cmd->len);
	}
}

void RedisClient::publish(const std::string& channel, const std::string& msg)
{
	if (pubs_)
	{
		PrintD("publish %s> %s", channel.c_str(), msg.c_str());
		redisAsyncCommand(pubs_, nullptr, nullptr, "publish %s %s", channel.c_str(), msg.c_str());
	}
}

void RedisClient::subsCallback(redisAsyncContext* ac, void* r, void* privdata)
{
	redisReply* reply = (redisReply*)r;
	if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3)
	{
		if (reply->element[2]->type == REDIS_REPLY_INTEGER)
		{
			PrintD("redis %s %s %lld", reply->element[0]->str, reply->element[1]->str, reply->element[2]->integer);
			return;
		}

		std::string channel, msg;
		if (0 == strcasecmp(reply->element[0]->str, "message"))
		{
			channel = reply->element[1]->str;
			msg.assign(reply->element[2]->str, reply->element[2]->len);
		}
		else if (reply->elements == 4 && !strcasecmp(reply->element[0]->str, "pmessage"))
		{
			channel = reply->element[2]->str;
			msg.assign(reply->element[3]->str, reply->element[3]->len);
		}

		subsCount++;
		PrintD("%s %s< %s", reply->element[0]->str, channel.c_str(), msg.c_str());
		if (auto redis = RedisClient::Instance())
		{
			MsgFunc cb = redis->getSubsCB(channel);
			if (cb)
			{
				// MS_DEBUG("%s< %s", channel.c_str(), msg.c_str());
				try
				{
					cb(msg);
				}
				catch (std::exception& e)
				{
					PrintW(">>> subs %s callback %s exception %s", channel.c_str(), msg.c_str(), e.what());
				}
			}
		}
	}
	else
	{
		PrintW("subsCallback error %p", reply);
	}
}

void RedisClient::subscribe(const std::string& channel, MsgFunc func)
{
	PrintD("subscribe %s", channel.c_str());
	subsMap[channel] = func;
	if (subs_)
	{
		redisAsyncCommand(subs_, &subsCallback, nullptr, "subscribe %s", channel.c_str());
	}
	if (!subsChecker_)
	{
		float timeout = 60;
		subsChecker_ = std::make_shared<Timer>(timeout, [this]() {
			OnTimer(1);
			return true;
		}, getLoop());
		PrintD("start subsChecker with interval %fs", timeout);
	}
}

void RedisClient::unsubscribe(const std::string& channel)
{
	PrintD("unsubscribe %s", channel.c_str());
	subsMap.erase(channel);
	if (subs_)
	{
		redisAsyncCommand(subs_, nullptr, nullptr, "unsubscribe %s", channel.c_str());
	}
}
