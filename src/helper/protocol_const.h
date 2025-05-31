#ifndef SRC_HELPER_PROTOCOL_CONST
#define SRC_HELPER_PROTOCOL_CONST

#define PROTOCOL_STATUS_INITIALISING 0 // Initialised > 1
#define PROTOCOL_STATUS_NO_DEVICE 1    // Start linking > 3
#define PROTOCOL_STATUS_ERROR 2        // Linked > 4, no device in sequence > 1
#define PROTOCOL_STATUS_LINKING 3      // Linked > 4, Failed in sequence > 2
#define PROTOCOL_STATUS_ONLINE 4       // Phone connected > 5, no device > 1
#define PROTOCOL_STATUS_CONNECTED 5    // Phone disconnected > 4, no device > 1

#define MAGIC 0x55aa55aa
#define MAGIC_ENC 0x55bb55bb

#define CMD_OPEN 1
#define CMD_PLUGGED 2
#define CMD_STATE 3
#define CMD_UNPLUGGED 4
#define CMD_TOUCH 5
#define CMD_VIDEO_DATA 6
#define CMD_AUDIO_DATA 7
#define CMD_CONTROL 8
#define CMD_UNKNOWN_9 9
#define CMD_APP_INFO 10
#define CMD_BLUETOOTH_INFO 13
#define CMD_WIFI_INFO 14
#define CMD_DEVICE_LIST 18
#define CMD_MANUFACTURER 20
#define CMD_UNKNOWN_22 22 // > int 2 + data, 3 - open camer 4 - close camera
#define CMD_JSON_CONTROL 25
#define CMD_UNKNOWN_38 38
#define CMD_MEDIA_INFO 42 
#define CMD_UNKNOWN_119 119 // > 0 EVT_APP_RESET
#define CMD_DEBUG_LOG 136 // < > int 1/2 ???
#define CMD_SEND_FILE 153 // < int size + ANSI string path \0 + int size + byte data
#define CMD_UNKNOWN_161 161 // > 0 EVT_APP_LOG_GET
#define CMD_HEARTBEAT 170 // < 0 every 2 second
#define CMD_UPDATE_PROGRESS 177 // > int ???
#define CMD_UPDATE_ISTATUS 187 // > int ???
#define CMD_BOX_SW_VERSION 204 // > 32 byte ANSI string box version
#define CMD_ENCRYPTION 240  // < int key, > 0 support encryption

struct ProtocolCmdEntry
{
    int cmd;
    const char *name;
};

const ProtocolCmdEntry protocolCmdList[] = {
    {CMD_OPEN, "Open"},
    {CMD_PLUGGED, "Plugged"},
    {CMD_STATE, "State"},
    {CMD_UNPLUGGED, "Unplugged"},
    {CMD_TOUCH, "Touch"},
    {CMD_VIDEO_DATA, "Video"},
    {CMD_AUDIO_DATA, "Audio"},
    {CMD_CONTROL, "Control Bin"},
    {CMD_APP_INFO, "AppInfo"},
    {CMD_BLUETOOTH_INFO, "Bluetooth Info"},
    {CMD_WIFI_INFO, "WiFi Info"},
    {CMD_DEVICE_LIST, "Device List"},
    {CMD_JSON_CONTROL, "Control JSON"},
    {CMD_MANUFACTURER, "Manufacturer"},
    {CMD_MEDIA_INFO, "Media info"},
    {CMD_DEBUG_LOG, "Debug"},    
    {CMD_SEND_FILE, "File"},
    {CMD_HEARTBEAT, "Heartbeat"},
    {CMD_BOX_SW_VERSION, "Box SW version"},
    {CMD_ENCRYPTION, "Encryption"}};


#endif /* SRC_HELPER_PROTOCOL_CONST */
