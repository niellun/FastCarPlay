#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <pigpiod_if2.h>

bool running = true;

void signal_handler(int signum) {
    running = false;
}

int main() {
    const int PIN_HOLD = 17;    // stays ON
    const int PIN_BLINK = 27;   // toggles
    const double ON_INTERVAL = 0.2;   // seconds ON
    const double OFF_INTERVAL = 0.8;  // seconds OFF

    signal(SIGINT, signal_handler);

    int pi = pigpio_start(NULL, NULL);
    if (pi < 0) {
        std::cerr << "Failed to connect to pigpiod daemon.\n";
        return 1;
    }


    set_mode(pi, PIN_HOLD, PI_OUTPUT);
    set_mode(pi, PIN_BLINK, PI_OUTPUT);

    gpio_write(pi, PIN_HOLD, 1);
    std::cout << "GPIO " << PIN_HOLD << " ON (constant), toggling GPIO " << PIN_BLINK << "...\n";

    while (running) {
        gpio_write(pi, PIN_BLINK, 1);
        std::this_thread::sleep_for(std::chrono::duration<double>(ON_INTERVAL));
        gpio_write(pi, PIN_BLINK, 0);
        std::this_thread::sleep_for(std::chrono::duration<double>(OFF_INTERVAL));
    }

    std::cout << "\nStopping...\n";
    gpio_write(pi, PIN_HOLD, 0);
    gpio_write(pi, PIN_BLINK, 0);
    pigpio_stop(pi);

    return 0;
}