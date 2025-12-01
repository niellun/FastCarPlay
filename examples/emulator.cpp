// lgpio_blink.cpp
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <lgpio.h>

bool running = true;

void signal_handler(int signum) {
    running = false;
}

int main() {
    const int PIN_HOLD = 17;   // stays ON
    const int PIN_BLINK = 27;  // toggles
    const double ON_INTERVAL  = 0.2;   // seconds
    const double OFF_INTERVAL = 0.8;   // seconds

    signal(SIGINT, signal_handler);

    // Open the GPIO chip (0 = /dev/gpiochip0 on every Pi)
    int h = lgGpiochipOpen(0);
    if (h < 0) {
        std::cerr << "Failed to open gpiochip0 (error " << h << ")\n";
        return 1;
    }

    // Claim pins as outputs
    int rc1 = lgGpioClaimOutput(h, 0, PIN_HOLD, 1);   // 1 = initial HIGH
    int rc2 = lgGpioClaimOutput(h, 0, PIN_BLINK, 0);  // 0 = initial LOW
    if (rc1 < 0 || rc2 < 0) {
        std::cerr << "Failed to claim pins (rc1=" << rc1 << ", rc2=" << rc2 << ")\n";
        lgGpiochipClose(h);
        return 1;
    }

    std::cout << "GPIO " << PIN_HOLD << " ON (constant), toggling GPIO " << PIN_BLINK << ".\n";

    while (running) {
        lgGpioWrite(h, PIN_BLINK, 1);
        std::this_thread::sleep_for(std::chrono::duration<double>(ON_INTERVAL));

        lgGpioWrite(h, PIN_BLINK, 0);
        std::this_thread::sleep_for(std::chrono::duration<double>(OFF_INTERVAL));
    }

    std::cout << "\nStopping...\n";
    lgGpioWrite(h, PIN_HOLD, 0);
    lgGpioWrite(h, PIN_BLINK, 0);

    lgGpiochipClose(h);
    return 0;
}