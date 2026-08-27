#include <string>
#include <iostream>
#include <memory>
#include <atomic>
#include <csignal>

#include "common/functions.h"
#include "common/logger.h"

#include "application.h"
#include "settings.h"

static const char *title = "Fast Car Play v0.9";

static std::atomic<Application *> g_app{nullptr};

static void onStopSignal(int)
{
    // async-signal-safe: lock-free atomic load + atomic store only
    Application *app = g_app.load(std::memory_order_acquire);
    if (app)
        app->stop();
}

static void installSignalHandlers()
{
    struct sigaction sa = {};
    sa.sa_handler = onStopSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
}

void start()
{
    set_log_level(Settings::loglevel);
    Settings::print();

    Application app;
    // Guard declared after app: it resets g_app BEFORE app is destroyed,
    // including on the exception paths out of app.start()
    struct Guard
    {
        ~Guard() { g_app.store(nullptr, std::memory_order_release); }
    } guard;
    g_app.store(&app, std::memory_order_release);
    installSignalHandlers();
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
