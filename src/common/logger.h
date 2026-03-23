#ifndef SRC_HELPER_LOGGER
#define SRC_HELPER_LOGGER

#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <string>

#define LOGGER_COLOR_RESET   "\033[0m"
#define LOGGER_COLOR_GRAY    "\033[90m"
#define LOGGER_COLOR_RED     "\033[31m"
#define LOGGER_COLOR_YELLOW  "\033[33m"
#define LOGGER_COLOR_CYAN    "\033[36m"
#define LOGGER_COLOR_WHITE   "\033[37m"

#define LOGGER_MESSAGE_SIZE  2048

#ifdef DISBALE_LOG

#define set_log_level(...) (false)
#define log_e(...) do { } while (0)
#define log_w(...) do { } while (0)
#define log_i(...) do { } while (0)
#define log_d(...) do { } while (0)
#define log_v(...) do { } while (0)
#define log_p(...) do { } while (0)

#else

#if defined(_MSC_VER)
#define LOGGER_CONTEXT __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#define LOGGER_CONTEXT __PRETTY_FUNCTION__
#else
#define LOGGER_CONTEXT __func__
#endif

#define set_log_level(levelValue) Logger::instance().setLevel(levelValue)

#define log_e(...) \
    do { \
        if (Logger::instance().enabled(Logger::Level::Error)) \
            Logger::instance().log(Logger::Level::Error, LOGGER_CONTEXT, __VA_ARGS__); \
    } while (0)

#define log_w(...) \
    do { \
        if (Logger::instance().enabled(Logger::Level::Warning)) \
            Logger::instance().log(Logger::Level::Warning, LOGGER_CONTEXT, __VA_ARGS__); \
    } while (0)

#define log_i(...) \
    do { \
        if (Logger::instance().enabled(Logger::Level::Info)) \
            Logger::instance().log(Logger::Level::Info, LOGGER_CONTEXT, __VA_ARGS__); \
    } while (0)

#define log_d(...) \
    do { \
        if (Logger::instance().enabled(Logger::Level::Debug)) \
            Logger::instance().log(Logger::Level::Debug, LOGGER_CONTEXT, __VA_ARGS__); \
    } while (0)

#define log_v(...) \
    do { \
        if (Logger::instance().enabled(Logger::Level::Verbose)) \
            Logger::instance().log(Logger::Level::Verbose, LOGGER_CONTEXT, __VA_ARGS__); \
    } while (0)

#define log_p(...) \
    do { \
        if (Logger::instance().enabled(Logger::Level::Protocol)) \
            Logger::instance().log(Logger::Level::Verbose, LOGGER_CONTEXT, __VA_ARGS__); \
    } while (0)    

class Logger
{
public:
    enum class Level : uint8_t
    {
        None     = 0,
        Error    = 1,
        Warning  = 2,
        Info     = 3,
        Debug    = 4,
        Verbose  = 5,
        Protocol = 6,
    };

    static Logger& instance();

    void setLevel(int level);
    bool enabled(Level msgLevel) const;

    void log(Level level, const char* context, const char* fmt, ...);

private:
    Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void vlog(Level level, const char* context, const char* fmt, std::va_list args);

    static const char* levelColor(Level level);
    static std::string className(const char* context);

private:
    mutable std::mutex _mutex;
    Level _level;
};

#endif

#endif /* SRC_HELPER_LOGGER */
