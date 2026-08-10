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

#include "about_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include "../ZuluControlI2CClient.h"  // I2C_API_VERSION
#include "../ZuluControl_config.h"
#include <cstdio>

namespace zuluide::display {

void AboutScreen::init(int index)
{
    Screen::init(index);
    forceDraw();
}

void AboutScreen::draw()
{
    // Logo by device type -- same selection SplashScreen makes. There is no
    // dedicated ZuluIDE logo bitmap in icons.h (only ZuluControl and ZuluSCSI
    // logos exist), so a ZuluIDE device falls through to the ZuluControl logo.
    switch (g_device_type)
    {
        case zulucontrol::config::DeviceType::ZuluSCSI:
            _fb->drawBitmap(6, 0, icon_zulu_scsi_logo, 115, 56);
            break;
        case zulucontrol::config::DeviceType::ZuluIDE:
            _fb->drawBitmap(6, 0, icon_zulu_ide_logo, 115, 39);
            break;
        default:
            _fb->drawBitmap(6, 0, icon_zulu_control_logo, 115, 56);
    }

    char banner[40];
    snprintf(banner, sizeof(banner), "I2C API v%s", I2C_API_VERSION);
    printCenteredText(banner, 56);
}

void AboutScreen::dismiss()
{
    ChangeScreen(DisplayScreenType::Main, -1);
}

void AboutScreen::shortUserPress() { dismiss(); }
void AboutScreen::shortEjectPress() { dismiss(); }
void AboutScreen::shortRotaryPress() { dismiss(); }
void AboutScreen::rotaryChange(int) { dismiss(); }

}  // namespace zuluide::display
