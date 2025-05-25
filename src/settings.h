#ifndef SRC_SETTINGS
#define SRC_SETTINGS

#include "helper/settings_base.h"

// The singleton “Settings” namespace
class Settings
{
public:
    static inline Setting<int> vendorid{"vendor-id", 4884};
    static inline Setting<int> productid{"product-id ", 5408};
    static inline Setting<bool> fullscreen{"fullscreen", true};
    static inline Setting<int> dpi{"dpi", 0};    
    static inline Setting<int> width{"width", 720};
    static inline Setting<int> height{"height", 576};
    static inline Setting<int> fps{"fps", 60};
    static inline Setting<int> sourceWidth{"source-width", 720};
    static inline Setting<int> sourceHeight{"source-height", 576};
    static inline Setting<int> sourceFps{"source-fps", 30};
    static inline Setting<bool> logging{"logging", false};
    static inline Setting<int> scaler{"scaler", 2};
    static inline Setting<int> queue{"queue-size", 32};
    static inline Setting<int> fontSize{"font-size", 30};
    static inline Setting<bool> encryption{"encryption", false};
    static inline Setting<bool> autoconnect{"autoconnect", true};
    static inline Setting<std::string> onConnect{"on-connect-script", ""};
    static inline Setting<std::string> onDisconnect{"on-disconnect-script", ""};        

    static void load(const std::string &filename);
    static void print();

private:
    static void trim(std::string &s);
};

#endif /* SRC_SETTINGS */
