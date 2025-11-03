#!/usr/bin/env python3
import time
import struct
from smbus2 import SMBus

ADS1115_ADDRESS = 0x48
FIFO_PATH = "/tmp/myfifo"
LONG_PRESS = 20
fifo_file = None

class ButtonProcessor:
    def __init__(self, voltages, callback, long_press_threshold):
        """
        :param voltages: List of voltages sorted in descending order.
        :param callback: Function to call when a button press is detected. Signature: callback(button_id: int, long_press: bool)
        :param long_press_threshold: Number of consecutive calls to consider a long press.
        """
        self.voltages = voltages
        self.callback = callback
        self.long_press_threshold = long_press_threshold
        self.current_id = 0
        self.previous_id = 0
        self.press_count = 0

    def process_voltage(self, voltage):
        """
        Process the next voltage reading.

        :param voltage: The voltage to process.
        """
        button_id = 0
        if voltage < self.voltages[0]:
            for v in self.voltages:
                button_id = button_id + 1
                if voltage > v:
                    break

        if button_id == 0:
            if self.previous_id > 0 and self.press_count < self.long_press_threshold:
                self.callback(self.previous_id, False)
            self.previous_id = 0
        elif self.previous_id == button_id:
            if self.press_count < self.long_press_threshold:
                self.press_count = self.press_count + 1
                if self.press_count == self.long_press_threshold:
                    self.callback(button_id, True)
        elif self.current_id == button_id:
            self.previous_id = button_id
            self.press_count = 1

        self.current_id = button_id
            
def read_diff(bus, mux):
    # 128 SPS, ±4.096V, single-shot (sufficient for 25 Hz)
    config = (0x8000                  # OS = start conversion
              | (mux << 12)           # MUX select
              | (0x01 << 9)           # ±4.096V
              | (0x01 << 8)           # single-shot
              | 0x00A3)               # 128 SPS, comparator off

    bus.write_i2c_block_data(ADS1115_ADDRESS, 0x01, [(config >> 8) & 0xFF, config & 0xFF])
    time.sleep(0.005)  # ~5ms conversion time at 250 SPS
    data = bus.read_i2c_block_data(ADS1115_ADDRESS, 0x00, 2)
    value = (data[0] << 8) | data[1]
    if value > 0x7FFF:
        value -= 0x10000
    return value * 4.096 / 32768

def open_fifo():
    global fifo_file
    try:
        fifo_file = open(FIFO_PATH, "wb", buffering=0)
    except FileNotFoundError:
        fifo_file = None
        return False
    return True

def send_key_code(i: int):
    global fifo_file    
    if fifo_file is None:
        if not open_fifo():
            return
    try:
        fifo_file.write(struct.pack("B", i))
        fifo_file.flush()
    except (BrokenPipeError, FileNotFoundError):
        fifo_file = None
        send_key_code(i)

# Define a callback for channel A that prints debug info.
def callback0(button_id: int, long_press: bool):
    print(f"[Channel 0] Button {button_id} pressed. Long press: {long_press}")

# Define a callback for channel B that prints debug info.
def callback1(button_id: int, long_press: bool):
    print(f"[Channel 1] Button {button_id} pressed. Long press: {long_press}")


def main():
    with SMBus(1) as bus:
        interval = 1/50
        # Configure voltage thresholds (example values). Adjust according to hardware.
        voltage_thresholds = [2.7, 1.9, 1.15, 0.45]

        # Create ButtonProcessor instances for each channel with its own callback.
        proc0 = ButtonProcessor(voltage_thresholds, callback0, LONG_PRESS)
        proc1 = ButtonProcessor(voltage_thresholds, callback1, LONG_PRESS)

        # Continuously read voltages and feed them to the processors.
        while True:
            loop_start = time.time()
            v0 = read_diff(bus, 0x04)  # AIN0-AIN3
            v1 = read_diff(bus, 0x05)  # AIN1-AIN3
            proc0.process_voltage(v0)
            proc1.process_voltage(v1)
            elapsed = time.time() - loop_start
            time.sleep(max(0, interval - elapsed))

if __name__ == "__main__":
    main()