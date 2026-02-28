#include <chrono>
#include <thread>
#include <csignal>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <cstdint>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <pulse/simple.h>
#include <pulse/error.h>

using namespace std;
using namespace chrono;

#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED_HZ 4000000
#define ADC_RATE 16000
#define BLOCK_SIZE 4096
#define ALPHA 1
#define CORR_POWER 0.1

volatile bool active = true;
int spi_fd = -1;
pa_simple *pa_handle = nullptr;

double filtered = 0.0;
double correction = 0.0;

uint8_t tx[2] = {0, 0}, rx[2] = {0, 0};

struct spi_ioc_transfer tr = {
    .tx_buf = (unsigned long)tx,
    .rx_buf = (unsigned long)rx,
    .len = 2,
    .speed_hz = SPI_SPEED_HZ,
    .bits_per_word = 8};

void handler(int sig)
{
    active = false;
}

void init_pulse()
{
    pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE,
        .rate = ADC_RATE,
        .channels = 1};

    pa_buffer_attr ba = {
        .maxlength = BLOCK_SIZE * sizeof(int16_t) * 4,
        .tlength = BLOCK_SIZE * sizeof(int16_t),
        .prebuf = BLOCK_SIZE * sizeof(int16_t) * 2,
        .minreq = BLOCK_SIZE * sizeof(int16_t)};

    int error;
    pa_handle = pa_simple_new(nullptr, "ADS8320", PA_STREAM_PLAYBACK, nullptr, "ADC", &ss, nullptr, &ba, &error);
    if (!pa_handle)
    {
        std::cerr << "PulseAudio error: " << pa_strerror(error) << std::endl;
        exit(1);
    }
}

void spi_init()
{
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0)
    {
        std::cerr << "SPI error: " << strerror(errno) << std::endl;
        exit(1);
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED_HZ;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0)
    {
        std::cerr << "SPI mode error: " << strerror(errno) << std::endl;
        exit(1);
    }
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
    {
        std::cerr << "SPI bits error: " << strerror(errno) << std::endl;
        exit(1);
    }
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    {
        std::cerr << "SPI speed error: " << strerror(errno) << std::endl;
        exit(1);
    }
}

int16_t spi_read()
{
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);

    uint32_t raw = (rx[0] << 11) | ((rx[1]) << 1);
    return static_cast<int16_t>(raw+INT16_MIN);
}

int main()
{
    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    spi_init();
    init_pulse();

    steady_clock::time_point next = steady_clock::now();
    const duration<double> step(1.0 / ADC_RATE);
    const steady_clock::duration step_duration = duration_cast<steady_clock::duration>(step);

    int16_t buffer[BLOCK_SIZE];
    while (active)
    {
        int min_val = INT16_MAX;
        int max_val = INT16_MIN;
        double sum = 0.0;

        auto block_start = steady_clock::now();
        for (int i = 0; i < BLOCK_SIZE && active; i++)
        {
            steady_clock::time_point now = steady_clock::now();
            if (now < next)
                std::this_thread::sleep_until(next);

            int16_t sample = spi_read();
            //filtered = ALPHA * sample + (1 - ALPHA) * filtered;
            buffer[i] = sample;

            if(sample<min_val)
                min_val=sample;
            if(sample>max_val)
                max_val=sample;

            sum += sample;
            next += step_duration;
        }

        int error;
        if (pa_simple_write(pa_handle, buffer, BLOCK_SIZE * sizeof(int16_t), &error) < 0) {
            std::cerr << "Pulse write error: " << pa_strerror(error) << std::endl;
        }

        double block_time = duration_cast<duration<double>>(steady_clock::now() - block_start).count();

        double avg = sum / BLOCK_SIZE;
        correction = correction + avg * CORR_POWER;

        double voltage = avg / 32768.0 * 5;

        printf("Block: %.4fs (expected: %.4fs) | min=%6d max=%6d corr=%8.1f avg=%8.1f → %.3fV \n",
               block_time, BLOCK_SIZE * 1.0 / ADC_RATE, min_val, max_val, correction, avg, voltage);
    }

    if (pa_handle)
    {
        pa_simple_drain(pa_handle, nullptr);
        pa_simple_free(pa_handle);
    }

    if (spi_fd >= 0)
        close(spi_fd);
}

