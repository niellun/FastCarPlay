#ifndef SRC_INTERFACE
#define SRC_INTERFACE

#include "renderer.h"
#include <string>

class Interface : public Renderer
{
public:
    Interface(SDL_Renderer *renderer);
    ~Interface();
    bool drawHome(bool force, int state);

private:
    int _state;
    RendererText _textDongle;
    RendererText _textInit;
    RendererText _textConnect;
    RendererText _textLaunch;            
    RendererImage _mainImage;    
};

#endif /* SRC_INTERFACE */
