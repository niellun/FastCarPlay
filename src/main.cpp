#include <string>
#include <iostream>
#include <memory>

#include "helper/functions.h"

#include "application.h"
#include "pipe_listener.h"
#include "settings.h"

static const char *title = "Fast Car Play v0.7";

void start()
{
    if (!Settings::logging)
        disable_cout();
    else
        Settings::print();

    if (Settings::keyPipe.value.length() > 2)
        PipeListener pipeListener(Settings::keyPipe.value.c_str());

    Application app;
    app.start(title);
}

int main(int argc, char **argv)
{
    std::cout << title << std::endl;
    if (argc > 2)
    {
        std::cerr << "  Usage: " << argv[0] << " [settings_file]" << std::endl;
        return 0;
    }
    try
    {
        if (argc == 2 && !Settings::load(argv[1]))
            return 1;

        start();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Main] Error > " << e.what() << std::endl;
        return 1;
    }
    return 0;
}