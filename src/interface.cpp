#include "interface.h"
#include "resource/background.h"
#include "resource/font.h"
#include "resource/colors.h"
#include "settings.h"
#include "helper/protocol_const.h"
#include <iostream>

Interface::Interface(SDL_Renderer *renderer)
    : Renderer(renderer), _state(0),
      _textDongle(font, font_len, Settings::fontSize),
      _textInit(font, font_len, Settings::fontSize),
      _textConnect(font, font_len, Settings::fontSize),
      _textLaunch(font, font_len, Settings::fontSize),
      _mainImage(background, background_len)
{
}

Interface::~Interface()
{
}

bool Interface::drawHome(bool force, int state)
{
    if (state == _state && !force)
        return false;

    _state = state;
    int width, height;
    SDL_GetRendererOutputSize(_renderer, &width, &height);
    SDL_RenderClear(_renderer);

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
