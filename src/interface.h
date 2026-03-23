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
    bool drawHome(bool force, int state);
    void debug(const char *text);

private:
    void drawDebug();

    int _state;
    bool _debug;
    RendererText _textDongle;
    RendererText _textInit;
    RendererText _textConnect;
    RendererText _textLaunch;
    RendererText _textDebug;
    RendererImage _mainImage;
    std::string _debugText;
};

#endif /* SRC_INTERFACE */
