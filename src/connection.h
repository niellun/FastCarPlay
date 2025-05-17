#ifndef SRC_CONNECTION
#define SRC_CONNECTION

#include <cstddef>
#include <cstdint>

void * connecliton_loop(void *arg);

void sendKey(int key);

using DataCallback = void(*)(const uint8_t*, size_t);
void RegisterDataCallback(DataCallback cb);

#endif /* SRC_CONNECTION */
