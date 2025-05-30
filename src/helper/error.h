#ifndef SRC_ERROR
#define SRC_ERROR

#include <string>
#include <stdexcept>
#include "helper/functions.h"

extern "C"
{
#include <libavutil/error.h>
}

class Error
{
public:
    Error()
        : _error(false), _text("")
    {
    }

    void set(const std::string &error)
    {
        _error = true;
        _text = error;
    }

    bool null(const void *ptr, const std::string &message = "")
    {
        if (!ptr)
        {
            _text = message;
            _error = true;
            return true;
        }

        return false;
    }

    bool zero(u_int32_t id, const std::string &message = "")
    {
        if (id==0)
        {
            _text = message;
            _error = true;
            return true;
        }

        return false;
    }

    bool avFail(int code, const std::string &message = "")
    {
        if (code == 0)
            return false;
        _text = message + avErrorText(code);
        _error = true;
        return true;
    }

    bool error() const
    {
        return _error;
    }

    const std::string &message() const
    {
        return _text;
    }

    void throwError() const
    {
        if (_error)
            throw std::runtime_error(_text);
    }

private:
    bool _error;
    std::string _text;
};

#endif /* SRC_ERROR */
