#include "interface.h"
#include "resource/background.h"
#include "resource/font.h"
#include "resource/colours.h"
#include "settings.h"
#include "protocol/protocol_const.h"

Interface::Interface(SDL_Renderer *renderer)
    : Renderer(renderer),
      _state(0),
      _debug(false),
      _textDongle(font, font_len, Settings::fontSize),
      _textInit(font, font_len, Settings::fontSize),
      _textConnect(font, font_len, Settings::fontSize),
      _textLaunch(font, font_len, Settings::fontSize),
      _textDebug(font, font_len, 15),
      _mainImage(background, background_len)
{
}

Interface::~Interface()
{
}

bool Interface::render(AVFrame *frame)
{
    if (!frame)
        return false;

    if (_render == nullptr || frame->width != _textureWidth || frame->height != _textureHeight)
    {
        clear();
        if (!prepare(frame, Settings::width, Settings::height))
            return false;
    }

    (this->*_render)(frame);
    SDL_RenderCopy(_renderer, _texture, &_sourceRect, nullptr);

    if (_debug)
    {
        drawDebug();
        _debug = false;
    }

    SDL_RenderPresent(_renderer);
    return true;
}

bool Interface::drawHome(bool force, int state)
{
    if (state == _state && !force)
        return false;

    _state = state;
    int width, height;
    SDL_GetRendererOutputSize(_renderer, &width, &height);

    _mainImage.draw(_renderer, width, height);
    if (state == PROTOCOL_STATUS_ERROR)
    {
        if (_textDongle.prepare(_renderer, "Connection error", colorError))
            _textDongle.draw(_renderer, 0.05 * width, 0.2 * height - _textDongle.height / 2);
    }
    else
    {
        if (_textDongle.prepare(_renderer, "Insert dongle", state == PROTOCOL_STATUS_NO_DEVICE ? color1 : color1_inactive))
            _textDongle.draw(_renderer, 0.05 * width, 0.2 * height - _textDongle.height / 2);
    }
    if (_textInit.prepare(_renderer, "Initialising", state == PROTOCOL_STATUS_LINKING ? color2 : color2_inactive))
        _textInit.draw(_renderer, 0.05 * width, 0.4 * height - _textInit.height / 2);
    if (_textConnect.prepare(_renderer, "Connect phone", state == PROTOCOL_STATUS_ONLINE ? color3 : color3_inactive))
        _textConnect.draw(_renderer, 0.05 * width, 0.6 * height - _textConnect.height / 2);
    if (_textLaunch.prepare(_renderer, "Launching", state == PROTOCOL_STATUS_CONNECTED ? color4 : color4_inactive))
        _textLaunch.draw(_renderer, 0.05 * width, 0.8 * height - _textLaunch.height / 2);

    SDL_RenderPresent(_renderer);
    return true;
}

void Interface::debug(const char *text)
{
    _debugText = text ? text : "";
    _debug = true;
}

void Interface::drawDebug()
{
    if (_debugText.empty())
        return;

    constexpr int padding = 8;
    constexpr int lineSpacing = 2;
    const SDL_Color debugColor = {0, 255, 255, 255};
    SDL_BlendMode previousBlendMode;
    Uint8 previousR, previousG, previousB, previousA;
    SDL_GetRenderDrawBlendMode(_renderer, &previousBlendMode);
    SDL_GetRenderDrawColor(_renderer, &previousR, &previousG, &previousB, &previousA);
    SDL_SetRenderDrawBlendMode(_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 150);

    size_t lineStart = 0;
    int y = padding;

    while (lineStart <= _debugText.size())
    {
        size_t lineEnd = _debugText.find('\n', lineStart);
        std::string line = _debugText.substr(lineStart, lineEnd - lineStart);
        if (_textDebug.prepare(_renderer, line, debugColor))
        {
            SDL_Rect backgroundRect = {
                0,
                y,
                static_cast<int>(_textDebug.width * Settings::aspectCorrection) + padding * 2,
                _textDebug.height};
            SDL_RenderFillRect(_renderer, &backgroundRect);
            _textDebug.draw(_renderer, padding, y);
        }
        y += _textDebug.height + lineSpacing;

        if (lineEnd == std::string::npos)
            break;

        lineStart = lineEnd + 1;
    }

    SDL_SetRenderDrawColor(_renderer, previousR, previousG, previousB, previousA);
    SDL_SetRenderDrawBlendMode(_renderer, previousBlendMode);
}
