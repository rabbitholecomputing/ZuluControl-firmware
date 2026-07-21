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

#include "splash_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include "../ZuluControl_config.h"
#include <cstdio>
#include <cstring>

namespace zuluide::display {

void SplashScreen::init(int index)
{
    Screen::init(index);
    forceDraw();
}

void SplashScreen::setBannerText(const char *text)
{
    snprintf(_bannerText, sizeof(_bannerText), "%s", text);
    forceDraw();
}

void SplashScreen::draw()
{
    switch(g_device_type)
    {
        case zulucontrol::config::DeviceType::ZuluSCSI :
            _fb->drawBitmap(6, 0, icon_zulu_scsi_logo, 115, 56);
            break;
        default:
            _fb->drawBitmap(6, 0, icon_zulu_control_logo, 115, 56);
    }

    printCenteredText(_bannerText, 56);
}

void SplashScreen::shortUserPress()
{
    ChangeScreen(DisplayScreenType::Settings, -1);
}

}  // namespace zuluide::display
