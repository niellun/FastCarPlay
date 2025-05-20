#ifndef SRC_HELPER_SETTINGS
#define SRC_HELPER_SETTINGS

#include "settings_base.h"

// The singleton “Settings” namespace
class Settings
{
public:
    static inline Setting<int> vendorid{"vendor-id", 4884};
    static inline Setting<int> productid{"product-id ", 5408};
    static inline Setting<bool> fullscreen{"fullscreen", false};
    static inline Setting<int> width{"width", 720};
    static inline Setting<int> height{"height", 576};
    static inline Setting<int> fps{"fps", 60};
    static inline Setting<int> sourceWidth{"source-width", 720};
    static inline Setting<int> sourceHeight{"source-height", 576};
    static inline Setting<int> sourceFps{"source-fps", 30};
    static inline Setting<bool> logging{"logging", true};
    static inline Setting<int> scaler{"scaler", 2};
    static inline Setting<int> queue{"queue-size", 32};    
    static inline Setting<int> fontSize{"font-size", 30};       

    static void load(const std::string &filename);
    static void print();

private:
    static void trim(std::string &s);
};

#endif /* SRC_HELPER_SETTINGS */
