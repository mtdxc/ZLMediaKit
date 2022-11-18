#pragma once
#include "hlog.h"
#include <memory>
#include <sstream>
#include <stdexcept>

#define PrintLog(level, ...) logger_print(hlog, level, __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define PrintT(...) PrintLog(LOG_LEVEL_DEBUG, ##__VA_ARGS__)
#define PrintD(...) PrintLog(LOG_LEVEL_DEBUG, ##__VA_ARGS__)
#define PrintI(...) PrintLog(LOG_LEVEL_INFO, ##__VA_ARGS__)
#define PrintW(...) PrintLog(LOG_LEVEL_WARN, ##__VA_ARGS__)
#define PrintE(...) PrintLog(LOG_LEVEL_ERROR, ##__VA_ARGS__)

#define MS_TRACE()
#define MS_ERROR PrintE
#define MS_THROW_ERROR(...) do { PrintE(__VA_ARGS__); throw std::runtime_error("MS_THROW_ERROR"); } while(false)
#define MS_DUMP PrintT
#define MS_DEBUG_2TAGS(tag1, tag2, ...) PrintD(__VA_ARGS__)
#define MS_WARN_2TAGS(tag1, tag2, ...) PrintW(__VA_ARGS__)
#define MS_DEBUG_TAG(tag, ...) PrintD(__VA_ARGS__)
#define MS_ASSERT(con, ...) do { if(!(con)) { PrintE(__VA_ARGS__); std::runtime_error("MS_ASSERT"); } } while(false)
#define MS_ABORT(...) do { PrintE(__VA_ARGS__); abort(); } while(false)
#define MS_WARN_TAG(tag, ...) PrintW(__VA_ARGS__)
#define MS_DEBUG_DEV PrintD

/**
* 日志上下文
*/
class LogContext : public std::ostringstream {
public:
    //_file,_function改成string保存，目的是有些情况下，指针可能会失效
    //比如说动态库中打印了一条日志，然后动态库卸载了，那么指向静态数据区的指针就会失效
    LogContext() = default;
    LogContext(int level, const char *file, const char *function, int line, const char *module_name, const char *flag);
    ~LogContext() = default;

    int _level;
    int _line;
    std::string _file;
    std::string _function;
    std::string _module_name;
    std::string _flag;

    const std::string &str();

private:
    bool _got_content = false;
    std::string _content;
};
using LogContextPtr = std::shared_ptr<LogContext>;

/**
 * 日志上下文捕获器
 */
class LogContextCapture {
public:
    using Ptr = std::shared_ptr<LogContextCapture>;

    LogContextCapture(logger_t* logger, int level, const char *file, const char *function, int line, const char *flag = "");
    LogContextCapture(const LogContextCapture &that);
    ~LogContextCapture();

    /**
     * 输入std::endl(回车符)立即输出日志
     * @param f std::endl(回车符)
     * @return 自身引用
     */
    LogContextCapture &operator<<(std::ostream &(*f)(std::ostream &));

    template<typename T>
    LogContextCapture &operator<<(T &&data) {
        if (_ctx)
            (*_ctx) << std::forward<T>(data);
        return *this;
    }

    void clear();

private:
    LogContextPtr _ctx;
    logger_t* _logger;
};

//用法: DebugL << 1 << "+" << 2 << '=' << 3;
#define WriteL(level) LogContextCapture(hlog, level, __FILENAME__, __FUNCTION__, __LINE__)
#define TraceL WriteL(LOG_LEVEL_DEBUG)
#define DebugL WriteL(LOG_LEVEL_DEBUG)
#define InfoL WriteL(LOG_LEVEL_INFO)
#define WarnL WriteL(LOG_LEVEL_WARN)
#define ErrorL WriteL(LOG_LEVEL_ERROR)

#define TraceT TraceL << getIdentifier() << " "
#define DebugT DebugL << getIdentifier() << " "
#define InfoT InfoL << getIdentifier() << " "
#define WarnT WarnL << getIdentifier() << " "
#define ErrorT ErrorL << getIdentifier() << " "