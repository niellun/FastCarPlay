#ifndef SRC_UX_UIMAGE
#define SRC_UX_UIMAGE

#include <SDL2/SDL.h>
#include <iostream>

class UImage
{
public:
    UImage(const void *img_data, int img_size)
    {
        SDL_RWops *img_rw = SDL_RWFromConstMem(img_data, img_size);
        if (!img_rw)
        {
            std::cerr << "[UX] SDL can't open image: " << SDL_GetError() << std::endl;
            return;
        }

        _surface = SDL_LoadBMP_RW(img_rw, 1);
        if (!_surface)
        {
            std::cerr << "[UX] Failed to create image surface: " << SDL_GetError() << std::endl;
            return;
        }

        Width = _surface->w;
        Height = _surface->h;
    };

    ~UImage()
    {
        if (_surface)
        {
            SDL_FreeSurface(_surface);
            _surface = nullptr;
        }
    };

    SDL_Texture *GetImage(SDL_Renderer *renderer)
    {
        return SDL_CreateTextureFromSurface(renderer, _surface);
    }

    int Width = 0;

    int Height = 0;

private:
    SDL_Surface *_surface = nullptr;
};

#endif