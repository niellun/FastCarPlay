#ifndef SRC_SETTINGS
#define SRC_SETTINGS

#include "helper/settings_base.h"

// The singleton “Settings” namespace
class Settings
{
public:
    // General section
    static inline Setting<int> vendorid{"vendor-id", 4884};
    static inline Setting<int> productid{"product-id ", 5408};
    static inline Setting<int> sourceWidth{"source-width", 720};
    static inline Setting<int> sourceHeight{"source-height", 576};
    static inline Setting<int> sourceFps{"source-fps", 50};
    static inline Setting<int> width{"width", 720};
    static inline Setting<int> height{"height", 576};
    static inline Setting<int> fps{"fps", 50};
    static inline Setting<bool> fullscreen{"fullscreen", true};
    static inline Setting<bool> logging{"logging", false};

    // Device configurations section
    static inline Setting<bool> encryption{"encryption", false};
    static inline Setting<bool> autoconnect{"autoconnect", true};
    static inline Setting<bool> weakCharge{"weak-charge", true};
    static inline Setting<bool> leftDrive{"left-hand-drive", true};
    static inline Setting<int> nightMode{"night-mode", 2};     
    static inline Setting<bool> wifi5{"wifi-5", true};   
    static inline Setting<bool> bluetoothAudio{"bluetooth-audio", false};    
    static inline Setting<int> micType{"mic-type", 1};               
    static inline Setting<int> dpi{"dpi", 0};

    // Application configuration section
    static inline Setting<int> fontSize{"font-size", 30};    
    static inline Setting<bool> vsync{"vsync", false};
    static inline Setting<float> aspectCorrection{"aspect-correction", 1};    
    static inline Setting<int> scaler{"scaler", 2};
    static inline Setting<bool> fastScale{"fast-render-scale", false};
    static inline Setting<int> videoQueue{"video-buffer-size", 32};
    static inline Setting<int> audioQueue{"audio-buffer-size", 16};
    static inline Setting<int> audioDelay{"audio-buffer-wait", 2};
    static inline Setting<float> audioFade{"audio-fade", 0.3};
    static inline Setting<std::string> onConnect{"on-connect-script", ""};
    static inline Setting<std::string> onDisconnect{"on-disconnect-script", ""};

    // Debug section
    static inline Setting<int> protocolDebug{"protocol-debug", 0};

    static void load(const std::string &filename);
    static void print();

private:
    static void trim(std::string &s);
};

#endif /* SRC_SETTINGS */
