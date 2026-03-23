#ifndef SRC_SETTINGS
#define SRC_SETTINGS

#include "common/settings_base.h"

#define SCREEN_MODE_WINDOW 0
#define SCREEN_MODE_FULLSCREEN 1
#define SCREEN_MODE_HEADLESS 2

// The singleton “Settings” namespace
class Settings
{
public:
    // General section
    static inline Setting<int> vendorid{"vendor-id", 4884};
    static inline Setting<int> productid{"product-id", 5408};
    static inline Setting<int> width{"width", 720};
    static inline Setting<int> height{"height", 576};
    static inline Setting<int> sourceFps{"source-fps", 60};
    static inline Setting<int> fps{"fps", 60};
    static inline Setting<int> screenMode{"window-mode", 0};
    static inline Setting<bool> cursor{"cursor", false};
    static inline Setting<int> loglevel{"log-level", 2};

    // Device configurations section
    static inline Setting<bool> encryption{"encryption", false};
    static inline Setting<bool> autoconnect{"autoconnect", true};
    static inline Setting<bool> weakCharge{"weak-charge", true};
    static inline Setting<bool> leftDrive{"left-hand-drive", true};
    static inline Setting<int> nightMode{"night-mode", 2};
    static inline Setting<bool> wifi5{"wifi-5", true};
    static inline Setting<bool> bluetoothAudio{"bluetooth-audio", false};
    static inline Setting<int> micType{"mic-type", 1};
    static inline Setting<int> dpi{"android-dpi", 120};
    static inline Setting<int> androidMode{"android-resolution", 1};
    static inline Setting<int> mediaDelay{"android-media-delay", 300};

    // Application configuration section
    static inline Setting<int> fontSize{"font-size", 30};
    static inline Setting<bool> vsync{"vsync", false};
    static inline Setting<bool> hwDecode{"hw-decode", true};
    static inline Setting<int> renderingBuffer{"rendering-buffer", 5};
    static inline Setting<int> eventsSkip{"draw-skip-events", 3};
    static inline Setting<int> forceRedraw{"force-redraw", 0};
    static inline Setting<float> aspectCorrection{"aspect-correction", 1};
    static inline Setting<std::string> renderDriver{"renderer-driver", ""};    
    static inline Setting<bool> alternativeRendering{"alternative-rendering", false};    
    static inline Setting<bool> fastScale{"fast-render-scale", false};
    static inline Setting<int> usbQueue{"async-usb-calls", 16};
    static inline Setting<int> usbTransferSize{"usb-buffer-size", 2048};         
    static inline Setting<int> usbBuffer{"usb-buffer", 64};        
    static inline Setting<int> videoQueue{"video-buffer-size", 64};
    static inline Setting<int> audioQueue{"audio-buffer-size", 64};
    static inline Setting<int> audioDelay{"audio-buffer-wait", 2};
    static inline Setting<int> audioDelayCall{"audio-buffer-wait-call", 6};
    static inline Setting<float> audioFade{"audio-fade", 0.3};
    static inline Setting<int> audioBuffer{"audio-buffer-samples", 512};
    static inline Setting<std::string> audioDriver{"audio-driver", ""};
    static inline Setting<std::string> onConnect{"on-connect-script", ""};
    static inline Setting<std::string> onDisconnect{"on-disconnect-script", ""};

    // Key mapping section
    static inline KeySetting<int> keySiri{"key-siri", 115, 5};
    static inline KeySetting<int> keyNightOn{"key-nightmode-on", 122, 16};
    static inline KeySetting<int> keyNightOff{"key-nightmode-off", 120, 17};
    static inline KeySetting<int> keyLeft{"key-left", 1073741904, 100};
    static inline KeySetting<int> keyRight{"key-right", 1073741903, 101};
    static inline KeySetting<int> keyEnter{"key-enter", 13, 104};
    static inline KeySetting<int> keyEnterUp{"key-enterup", 0, 105};
    static inline KeySetting<int> keyBack{"key-back", 8, 106};
    static inline KeySetting<int> keyUp{"key-up", 1073741906, 113};
    static inline KeySetting<int> keyDown{"key-down", 1073741905, 114};
    static inline KeySetting<int> keyHome{"key-home", 104, 200};
    static inline KeySetting<int> keyPlay{"key-play", 93, 201};
    static inline KeySetting<int> keyPause{"key-pause", 91, 202};
    static inline KeySetting<int> keyPlayPause{"key-play-toggle", 112, 203};
    static inline KeySetting<int> keyNext{"key-next", 46, 204};
    static inline KeySetting<int> keyPrev{"key-previous", 44, 205};
    static inline KeySetting<int> keyAccept{"key-call-accept", 97, 300};
    static inline KeySetting<int> keyReject{"key-call-reject", 115, 301};
    static inline KeySetting<int> keyVideoFocus{"key-video-focus", 118, 500};
    static inline KeySetting<int> keyVideoRelease{"key-video-release", 98, 501};
    static inline KeySetting<int> keyNavFocus{"key-nav-focus", 110, 508};
    static inline KeySetting<int> keyNavRelease{"key-nav-release", 109, 509};
    static inline Setting<std::string> keyPipe{"key-pipe-path", ""};

    // Debug section
    static inline Setting<bool> codecLowDelay{"decode-low-delay", true};
    static inline Setting<bool> codecFast{"decode-fast", true};
    static inline Setting<bool> debugOverlay{"debug-overlay", false};

    static bool load(const std::string &filename);
    static void print();

    static inline bool isFullscreen() { return screenMode == SCREEN_MODE_FULLSCREEN; };
    static inline bool isHeadless() { return screenMode == SCREEN_MODE_HEADLESS; };

private:
    static void trim(std::string &s);
};

#endif /* SRC_SETTINGS */
