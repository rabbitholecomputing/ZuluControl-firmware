/**
 * ZuluIDE™ - Copyright (c) 2026 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

#include "settings_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include "../ZuluControlI2CClient.h"  // I2C_API_VERSION
#include <cstdio>

namespace zuluide::display {

void SettingsScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();
    forceDraw();
}

void SettingsScreen::tick()
{
    _data->Refresh();
    forceDraw();
    Screen::tick();
}

void SettingsScreen::draw()
{
    _fb->drawText(0, 0, "Settings");
    _fb->drawHLine(0, 10, 112);

    if (_data->SdPresent())
        _fb->drawBitmap(115, 0, icon_sd, 12, 12);
    else
        _fb->drawBitmap(115, 0, icon_nosd, 12, 12);

    _fb->drawBitmap(0, 14, icon_select, 8, 8);
    _fb->drawText(10, 15, "About");
}

void SettingsScreen::showAbout()
{
    char banner[40];
    snprintf(banner, sizeof(banner), "I2C API v%s", I2C_API_VERSION);
    _splash->setBannerText(banner);
    ChangeScreen(DisplayScreenType::Splash, -1);
}

void SettingsScreen::shortRotaryPress()
{
    showAbout();
}

void SettingsScreen::shortEjectPress()
{
    showAbout();
}

void SettingsScreen::shortUserPress()
{
    ChangeScreen(DisplayScreenType::Main, -1);
}

}  // namespace zuluide::display
