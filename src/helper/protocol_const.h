#ifndef SRC_HELPER_PROTOCOL_CONST
#define SRC_HELPER_PROTOCOL_CONST

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
#define CMD_JSON_CONTROL 25
#define CMD_MANUFACTURER 20
#define CMD_UNKNOWN_38 38
#define CMD_MEDIA_INFO 42
#define CMD_SEND_FILE 153
#define CMD_UNKNOWN_136 136
#define CMD_DAYNIGHT 162
#define CMD_HEARTBEAT 170
#define CMD_VERSION 204
#define CMD_ENCRYPTION 240

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
    {CMD_SEND_FILE, "File"},
    {CMD_DAYNIGHT, "DeyNight Mode"},
    {CMD_HEARTBEAT, "Heartbeat"},
    {CMD_VERSION, "Version"},
    {CMD_ENCRYPTION, "Encryption"}};


#endif /* SRC_HELPER_PROTOCOL_CONST */
