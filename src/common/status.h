#ifndef SRC_STRUCT_STATUS
#define SRC_STRUCT_STATUS

#include <cstdarg>
#include <cstdio>

class Status
{
public:
    static Status Success()
    {
        return Status();
    }

    static Status Error(const char* format, ...)
    {
        std::va_list args;
        va_start(args, format);
        Status status(format, args);
        va_end(args);

        return status;
    }

    bool succeed() const
    {
        return _result;
    }

    bool failed() const
    {
        return !_result;
    }    

    explicit operator bool() const
    {
        return _result;
    }

    const char* error() const
    {
        return _message;
    }

private:
    static constexpr int MESSAGE_SIZE = 256;

    Status()
        : _result(true)
    {
        _message[0] = '\0';
    }

    Status(const char* format, std::va_list args)
        : _result(false)
    {
        std::vsnprintf(_message, sizeof(_message), format ? format : "", args);
    }

    bool _result;
    char _message[MESSAGE_SIZE];
};

#endif /* SRC_STRUCT_STATUS */
