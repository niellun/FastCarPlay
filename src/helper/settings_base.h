#ifndef SRC_HELPER_SETTINGS_BASE
#define SRC_HELPER_SETTINGS_BASE

#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <stdexcept>

// Base interface for any one setting
class ISetting
{
public:
    std::string name;
    virtual void parse(std::string &str) = 0;
    virtual std::string asString() const = 0;    
};

// Holds the global registry
inline std::vector<ISetting *> &_settings()
{
    static std::vector<ISetting *> settings;
    return settings;
}

// A “typed” setting that auto‑registers itself
template <typename T>
class Setting : public ISetting
{
public:
    T value;
    Setting(std::string name_, T default_)
        : value(default_)
    {
        name = std::move(name_);
        _settings().push_back(this);
    }

    // allow using this as if it were a T
    operator T() const { return value; }
    Setting &operator=(T newValue)
    {
        value = newValue;
        return *this;
    }

    // parse a string into T
    void parse(std::string &str) override
    {
        try
        {
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
            if constexpr (std::is_same_v<T, bool>)
            {
                if (str == "1" || str == "true")
                    value = true;
                else if (str == "0" || str == "false")
                    value = false;
                else
                    throw new std::runtime_error("Can't convert to boolean.");
            }
            else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                value = static_cast<T>(std::stoll(str));
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                value = static_cast<T>(std::stold(str));
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                value = str;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Settings] failed to parse \"" << str
                      << "\" for key \"" << name << "\": " << e.what() << std::endl;
        }
    }

    std::string asString() const override
    {
        if constexpr (std::is_same_v<T, bool>)
            return value ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::string>)
            return value;
        else
            return std::to_string(value);
    }
};

#endif /* SRC_HELPER_SETTINGS_BASE */
