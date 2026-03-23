#include "settings.h"

#include <iostream>
#include <fstream>

#include "common/logger.h"

bool Settings::load(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        log_e( "Cannot open file > %s", filename.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // strip comments
        std::size_t p = line.find('#');
        if (p != std::string::npos)
            line.erase(p);

        // trim
        trim(line);
        if (line.empty())
            continue;

        // split on '='
        std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);

        // lookup in registry
        bool found = false;
        for (ISetting *setting : _settings())
        {
            if (setting->name == key)
            {
                setting->parse(value);
                found = true;
                break;
            }
        }
        if (!found)
            log_w("Unknown key > %s", key.c_str());
    }
    return true;
}

void Settings::print()
{
    for (ISetting *setting : _settings())
    {
        log_d("%s = %s",setting->name.c_str(), setting->asString().c_str());
    }
}

void Settings::trim(std::string &s)
{
    // Left trim
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(),
                         [](unsigned char c)
                         { return !std::isspace(c); }));

    // Right trim
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char c)
                         { return !std::isspace(c); })
                .base(),
            s.end());
}
