#ifndef SRC_RENDERER
#define SRC_RENDERER

extern "C"
{
#include <libavformat/avformat.h> // FFmpeg library for multimedia container format handling
#include <libswscale/swscale.h>   // FFmpeg library for image scaling and pixel format conversion
}

#include <SDL2/SDL.h>
#include <string>

class Renderer
{
public:
    Renderer(SDL_Renderer *renderer);
    ~Renderer();

    bool prepare(AVFrame *frame, int targetWidth, int targetHeight, uint32_t scaler);
    bool render(AVFrame *frame);

    SDL_Texture *texture;
    int textureWidth;
    int textureHeight;

private:
    using DrawFuncType = void (Renderer::*)(AVFrame *);

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
    void yuv(AVFrame *frame);
    void scale(AVFrame *frame);

    SDL_Renderer *_renderer;
    DrawFuncType _render;
    SwsContext *_sws;
    int _swsWidth;
    int _swsHeight;
    AVFrame *_frame;
    static const FormatMapping _mapping[];
};

#endif /* SRC_RENDERER */
