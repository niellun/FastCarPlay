

/**
 * @brief Example code for intercepting resistive button presses via an ADS1115 ADC module on a Raspberry Pi.
 *
 * This file demonstrates how to read the voltage levels from the button wires connected to an
 * ADS1115 analog‑to‑digital converter over I²C. The voltages are decoded into button
 * identifiers, long‑press events are detected, and corresponding key codes are sent to the
 * main application through a named FIFO pipe.
 *
 * Important:`FIFO_PATH` must match the pipe name configured in the main application
 * settings.
 *
 * @note The program ignores `SIGPIPE` to avoid termination when the pipe is closed.
 * @note Compile with a C++17 (or newer) compiler and link against the I²C library.
 */

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdint>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <signal.h>

#define FIFO_PATH "/tmp/fastcarplay_pipe"
#define LONG_PRESS 20
#define ADS1115_ADDRESS 0x48
#define FREQUENCY 50

#define BTN_LEFT 100
#define BTN_RIGHT 101
#define BTN_SELECT_DOWN 104
#define BTN_SELECT_UP 105
#define BTN_BACK 106
#define BTN_HOME 200

int fifo_fd = -1;
std::vector<double> voltages = {2.7, 1.9, 1.15, 0.45};

class ButtonProcessor
{
public:
    ButtonProcessor(std::function<void(int, bool)> callback, int long_press)
        : _callback(callback), _long_press(long_press),
          _current(0), _previous(0), _count(0) {}

    void process_voltage(double voltage)
    {
        int button_id = 0;
        if (voltage < voltages[0])
        {
            for (auto v : voltages)
            {
                button_id++;
                if (voltage > v)
                    break;
            }
        }

        if (button_id == 0)
        {
            if (_previous > 0 && _count < _long_press)
            {
                _callback(_previous, false);
            }
            _previous = 0;
        }
        else if (_previous == button_id)
        {
            if (_count < _long_press)
            {
                _count++;
                if (_count == _long_press)
                {
                    _callback(button_id, true);
                }
            }
        }
        else if (_current == button_id)
        {
            _previous = button_id;
            _count = 1;
        }

        _current = button_id;
    }

private:
    std::function<void(int, bool)> _callback;
    int _long_press;
    int _current;
    int _previous;
    int _count;
};

void send_key_code(uint8_t i)
{
    if (fifo_fd == -1)
    {
        fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
        if (fifo_fd == -1)
            return; // cannot open FIFO
    }

    ssize_t n = write(fifo_fd, &i, sizeof(i));
    if (n == -1)
    {
        // handle broken pipe or closed reader
        close(fifo_fd);
        fifo_fd = -1;
    }
}

// Callback examples
void callback0(int button_id, bool long_press)
{
    std::cout << "[Channel 0] Button " << button_id << " pressed. Long press: " << long_press << "\n";
    if (button_id == 4)
    {
        if (long_press)
        {
            send_key_code(BTN_SELECT_DOWN);
            send_key_code(BTN_SELECT_UP);
        }
        else
        {
            send_key_code(BTN_RIGHT);
        }
    }

    if (button_id == 3)
    {
        if (long_press)
        {
            send_key_code(BTN_BACK);
        }
        else
        {
            send_key_code(BTN_LEFT);
        }
    }
}

void callback1(int button_id, bool long_press)
{
    std::cout << "[Channel 1] Button " << button_id << " pressed. Long press: " << long_press << "\n";
    if (button_id == 3)
    {
        if (!long_press)
        {
            send_key_code(BTN_HOME);
        }
    }    
}

double read_diff(int file_i2c, uint8_t mux)
{
    uint16_t config = 0x8000        // OS = start conversion
                      | (mux << 12) // MUX
                      | (0x01 << 9) // ±4.096V
                      | (0x01 << 8) // single-shot
                      | 0x00A3;     // 128 SPS, comparator off

    uint8_t buffer[3];
    buffer[0] = 0x01; // Config register
    buffer[1] = (config >> 8) & 0xFF;
    buffer[2] = config & 0xFF;

    write(file_i2c, buffer, 3);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    uint8_t read_buf[2];
    buffer[0] = 0x00; // Conversion register
    write(file_i2c, buffer, 1);
    read(file_i2c, read_buf, 2);

    int16_t value = (read_buf[0] << 8) | read_buf[1];

    return static_cast<double>(value) * 4.096 / 32768.0;
}

int main()
{
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE globally

    int file_i2c = open("/dev/i2c-1", O_RDWR);
    if (file_i2c < 0 || ioctl(file_i2c, I2C_SLAVE, ADS1115_ADDRESS) < 0)
    {
        std::cerr << "Failed to open I2C device\n";
        return 1;
    }

    ButtonProcessor proc0(callback0, LONG_PRESS);
    ButtonProcessor proc1(callback1, LONG_PRESS);

    constexpr double interval = 1.0 / FREQUENCY;

    while (true)
    {
        auto loop_start = std::chrono::steady_clock::now();
        double v0 = read_diff(file_i2c, 0x04);
        double v1 = read_diff(file_i2c, 0x05);
        proc0.process_voltage(v0);
        proc1.process_voltage(v1);
        auto loop_end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = loop_end - loop_start;
        if (elapsed.count() < interval)
        {
            std::this_thread::sleep_for(std::chrono::duration<double>(interval - elapsed.count()));
        }
    }
    close(file_i2c);
    return 0;
}
