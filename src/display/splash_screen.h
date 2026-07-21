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

// Port of ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/SplashScreen.h/.cpp:
// a "dumb" screen that just draws the logo plus whatever one-line banner
// was last set via setBannerText() -- it has no timer of its own. Two
// things reuse it: display_task.cpp's boot sequence (banner cycles from
// this project's firmware version to the connection status, then moves on
// to Main -- mirrors control.cpp's splashScreenPoll() external state
// machine, ZuluSCSI-firmware's SplashScreen.cpp is equally dumb) and
// SettingsScreen's "About" entry (banner set to the I2C API version,
// with no auto-advance -- shortUserPress() returns to Settings, matching
// SplashScreen.cpp:30-33's "About" round-trip).

#pragma once

#include "screen.h"

namespace zuluide::display {

class SplashScreen : public Screen
{
public:
    explicit SplashScreen(Framebuffer128x64 *fb) : Screen(fb) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Splash; }

    void init(int index) override;
    void draw() override;

    void shortUserPress() override;

    void setBannerText(const char *text);

private:
    char _bannerText[40] = "";
};

}  // namespace zuluide::display
