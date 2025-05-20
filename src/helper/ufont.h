#ifndef SRC_HELPER_UFONT
#define SRC_HELPER_UFONT

#include <SDL2/SDL_ttf.h>
#include <iostream>

class UFont
{
public:
    UFont(const void *font_data, int data_size, int ptsize)
    {
        SDL_RWops *font_rw = SDL_RWFromConstMem(font_data, data_size);
        if (!font_rw)
        {
            std::cerr << "[UX] SDL can't open font: " << SDL_GetError() << std::endl;
            return;
        }

        _font = TTF_OpenFontRW(font_rw, 1, ptsize);
        if (!_font)
        {
            std::cerr << "[UX] SDL can't load font: " << TTF_GetError() << std::endl;
        }
    };

    ~UFont()
    {
        if (_font)
        {
            TTF_CloseFont(_font);
            _font = nullptr;
        }
    };

    SDL_Texture *GetText(SDL_Renderer *renderer, const char *text, SDL_Color color)
    {
        if (!_font)
            return nullptr;

        SDL_Surface *textSurface = TTF_RenderText_Blended(_font, text, color);
        if (!textSurface)
        {
            std::cerr << "[UX] Failed to create text surface: " << TTF_GetError() << std::endl;
            return nullptr;
        }

        SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
        SDL_FreeSurface(textSurface);
        if (!textTexture)
        {
            std::cerr << "[UX] Failed to create text texture: " << TTF_GetError() << std::endl;
            return nullptr;
        }

        return textTexture;
    }

private:
    TTF_Font *_font = nullptr;
};

#endif /* SRC_HELPER_UFONT */
