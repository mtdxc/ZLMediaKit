#include "logger.h"
#include "hbase.h"

static inline const char *getFileName(const char *file) {
    auto pos = strrchr(file, '/');
#ifdef _WIN32
    if (!pos) {
        pos = strrchr(file, '\\');
    }
#endif
    return pos ? pos + 1 : file;
}

static inline const char *getFunctionName(const char *func) {
#ifndef _WIN32
    return func;
#else
    auto pos = strrchr(func, ':');
    return pos ? pos + 1 : func;
#endif
}

LogContext::LogContext(int level, const char *file, const char *function, int line, const char *module_name, const char *flag)
    : _level(level), _line(line), _file(getFileName(file)), _function(getFunctionName(function)),
    _module_name(module_name), _flag(flag) {
    //_thread_name = getThreadName();
}

const std::string &LogContext::str() {
    if (_got_content) {
        return _content;
    }
    _content = std::ostringstream::str();
    _got_content = true;
    return _content;
}

///////////////////AsyncLogWriter///////////////////
static char buff[64];
static std::string s_module_name = get_executable_file(buff, 64);

LogContextCapture::LogContextCapture(logger_t* logger, int level, const char *file, const char *function, int line, const char *flag) :
    _ctx(new LogContext(level, file, function, line, s_module_name.c_str(), flag)), _logger(logger) {
}

LogContextCapture::LogContextCapture(const LogContextCapture &that) : _ctx(that._ctx), _logger(that._logger) {
    const_cast<LogContextPtr &>(that._ctx).reset();
}

LogContextCapture::~LogContextCapture() {
    *this << std::endl;
}

LogContextCapture &LogContextCapture::operator<<(std::ostream &(*f)(std::ostream &)) {
    if (_ctx) {
        logger_print(_logger, _ctx->_level, _ctx->_file.c_str(), _ctx->_line, _ctx->_function.c_str(), "%s", _ctx->str().c_str());
        _ctx.reset();
    }
    return *this;
}

void LogContextCapture::clear() {
    _ctx.reset();
}
