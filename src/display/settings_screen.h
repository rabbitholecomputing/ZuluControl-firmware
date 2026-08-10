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

// Partial port of ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/
// SettingsScreen.h/.cpp: only the "About" entry is populated this
// iteration (task requirement); the other 5 original entries (Debug log,
// SD log, Reboot, Mass SD/Image Mode) have no equivalent action available
// over this protocol yet and are left for the next iteration rather than
// listed as dead menu items. Selecting About reuses SplashScreen exactly
// like the original (SettingsScreen.cpp:106-111): sets its banner text and
// switches to it, with no auto-advance timer this time (see
// splash_screen.h).

#pragma once

#include "screen.h"
#include "display_data.h"
#include "splash_screen.h"

namespace zuluide::display {

class SettingsScreen : public Screen
{
public:
    SettingsScreen(Framebuffer128x64 *fb, DisplayData *data, SplashScreen *splash)
        : Screen(fb), _data(data), _splash(splash) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Settings; }

    void init(int index) override;
    void draw() override;
    void tick() override;

    void shortRotaryPress() override;
    void shortEjectPress() override;
    void shortUserPress() override;

private:
    DisplayData *_data;
    SplashScreen *_splash;

    void showAbout();
};

}  // namespace zuluide::display
