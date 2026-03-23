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
};

class Renderer
{
public:
    Renderer(SDL_Renderer *renderer);
    ~Renderer();

    float xScale;
    float yScale;

protected:
    using DrawFuncType = void (Renderer::*)(AVFrame *);

    SDL_Renderer *_renderer;
    void clear();
    bool prepare(AVFrame *frame, int targetWidth, int targetHeight);
    SDL_Texture *_texture;
    int _textureWidth;
    int _textureHeight;
    SDL_Rect _sourceRect;
    DrawFuncType _render;

private:    
    struct FormatMapping
    {
        AVPixelFormat avFormat;
        SDL_PixelFormatEnum sdlFormat;
        DrawFuncType function;
        std::string name;
    };

    bool prepareTexture(uint32_t format, int width, int height);
    void rgb(AVFrame *frame);
    void nv(AVFrame *frame);
    void nvAlternative(AVFrame *frame);
    void yuv(AVFrame *frame);
    void yuvAlternative(AVFrame *frame);
    void scale(AVFrame *frame);

    SwsContext *_sws;
    AVFrame *_frame;
    FormatMapping _mapping[4] = {
        {AV_PIX_FMT_RGB24, SDL_PIXELFORMAT_RGB24, &Renderer::rgb, "RGB24"},
        {AV_PIX_FMT_YUV420P, SDL_PIXELFORMAT_IYUV, &Renderer::yuv, "YUV420P"},
        {AV_PIX_FMT_YUVJ420P, SDL_PIXELFORMAT_IYUV, &Renderer::yuv, "YUVJ420P"},
        {AV_PIX_FMT_NV12, SDL_PIXELFORMAT_NV12, &Renderer::nv, "NV12"}};
};

#endif /* SRC_RENDERER */
