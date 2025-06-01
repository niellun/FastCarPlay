#ifndef SRC_RENDERER
#define SRC_RENDERER

extern "C"
{
#include <libavformat/avformat.h> // FFmpeg library for multimedia container format handling
#include <libswscale/swscale.h>   // FFmpeg library for image scaling and pixel format conversion
}

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

class RendererText
{
public:
    RendererText(const void *font_data, int data_size, int ptsize);
    ~RendererText();
    bool prepare(SDL_Renderer *renderer, std::string text, SDL_Color color);  
    SDL_Rect draw(SDL_Renderer *renderer, int x, int y);
    int width;
    int height;

private:
    SDL_Texture *getText(SDL_Renderer *renderer, const char *text, SDL_Color color);
    static int sameColor(SDL_Color c1, SDL_Color c2) { return (c1.r == c2.r) && (c1.g == c2.g) && (c1.b == c2.b) && (c1.a == c2.a); }
    TTF_Font *_font = nullptr;
    SDL_Texture *_texture = nullptr;
    std::string _text;
    SDL_Color _color;
};

class RendererImage
{
public:
    RendererImage(const void *img_data, int img_size);
    ~RendererImage();
    SDL_Rect draw(SDL_Renderer *renderer, int w, int h);
    int width;
    int height;

private:
    SDL_Surface *_surface = nullptr;
    SDL_Texture *_texture = nullptr;    
    float _aspect;
};

class Renderer
{
public:
    Renderer(SDL_Renderer *renderer);
    ~Renderer();

    bool prepare(AVFrame *frame, int targetWidth, int targetHeight, uint32_t scaler, bool android, float *cropX, float *cropY);
    bool render(AVFrame *frame);

protected:
    SDL_Renderer *_renderer;

private:
    using DrawFuncType = void (Renderer::*)(AVFrame *);

    struct FormatMapping
    {
        AVPixelFormat avFormat;
        SDL_PixelFormatEnum sdlFormat;
        DrawFuncType function;
        DrawFuncType functionCrop;
        std::string name;
        uint8_t bpp;
    };

    void clear();    
    bool prepareTexture(uint32_t format, int width, int height);

    void rgb(AVFrame *frame);
    void nv(AVFrame *frame);
    void yuv(AVFrame *frame);
    void crgb(AVFrame *frame);
    void cnv(AVFrame *frame);
    void cyuv(AVFrame *frame);
    void cscale(AVFrame *frame);
    void scale(AVFrame *frame);

    SDL_Texture *_texture;
    int _textureWidth;
    int _textureHeight;
    int _cropX;
    int _cropY;
    bool _crop;
    uint8_t _bytesPerPixel;

    DrawFuncType _render;
    SwsContext *_sws;
    AVFrame *_frame;
    static const FormatMapping _mapping[];
};

#endif /* SRC_RENDERER */
