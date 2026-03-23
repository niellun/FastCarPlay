#include "logger.h"

#ifndef DISBALE_LOG

#include <cstdio>
#include <ctime>

Logger::Logger()
    : _level(Level::None)
{
}

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::setLevel(int level)
{
    if (level < static_cast<int>(Level::None))
    {
        _level = Level::None;
        return;
    }

    if (level > static_cast<int>(Level::Verbose))
    {
        _level = Level::Verbose;
        return;
    }

    _level = static_cast<Level>(level);
}

bool Logger::enabled(Level msgLevel) const
{
    return static_cast<int>(msgLevel) <= static_cast<int>(_level);
}

void Logger::log(Level level, const char *context, const char *fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    vlog(level, context, fmt, args);
    va_end(args);
}

void Logger::vlog(Level level, const char *context, const char *fmt, std::va_list args)
{
    char timebuf[32] = "--:--:--";
    const std::time_t now = std::time(nullptr);
    const std::tm *tmv = std::localtime(&now);
    if (tmv != nullptr)
        std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tmv);

    char message[LOGGER_MESSAGE_SIZE];
    std::vsnprintf(message, LOGGER_MESSAGE_SIZE, fmt, args);

    std::fprintf(
        (level <= Level::Warning) ? stderr : stdout,
        "%s%s %s[%s] %s%s\n",
        LOGGER_COLOR_GRAY,
        timebuf,
        levelColor(level),
        className(context).c_str(),
        message,
        LOGGER_COLOR_RESET);
}

const char *Logger::levelColor(Level level)
{
    switch (level)
    {
    case Level::Error:
        return LOGGER_COLOR_RED;
    case Level::Warning:
        return LOGGER_COLOR_YELLOW;
    case Level::Info:
        return LOGGER_COLOR_CYAN;
    case Level::Debug:
        return LOGGER_COLOR_RESET;
    case Level::Verbose:
        return LOGGER_COLOR_GRAY;
   case Level::Protocol:
        return LOGGER_COLOR_GRAY;        
    default:
        return LOGGER_COLOR_RESET;
    }
}

std::string Logger::className(const char *context)
{
    if (!context || *context == '\0')
        return "Global";

    std::string signature(context);
    const std::size_t scopePos = signature.rfind("::");
    if (scopePos == std::string::npos)
        return "Global";

    std::string scope = signature.substr(0, scopePos);
    const std::size_t spacePos = scope.find_last_of(" \t");
    if (spacePos != std::string::npos)
        scope = scope.substr(spacePos + 1);

    return scope.empty() ? "Global" : scope;
}

#endif
