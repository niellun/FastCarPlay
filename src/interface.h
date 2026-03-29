#ifndef SRC_INTERFACE
#define SRC_INTERFACE

#include "renderer.h"
#include <string>

class Interface : public Renderer
{
public:
    Interface(SDL_Renderer *renderer);
    ~Interface();
    bool render(AVFrame *frame);
    bool drawHome(bool force, int state, std::string name);
    void debug(const char *text);
    void showToast(const std::string &text);
    void hideToast();

private:
    void drawDebug();
    void drawToast();

    int _state;
    bool _debug;
    bool _toast;
    RendererText _textStatus;
    RendererText _textDebug;
    RendererText _textToast;
    RendererImage _mainImage;
    std::string _debugText;
    std::string _toastText;
};

#endif /* SRC_INTERFACE */
