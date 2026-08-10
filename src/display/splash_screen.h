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
// a "dumb" screen that just draws the logo plus whatever banner text was
// last set via setBannerText()/setSubBannerText() -- it has no timer of its
// own. Two things reuse it: display_task.cpp's boot sequence (banner cycles
// from this project's firmware version to the connection status, holding
// the whole time on a second line set once via setSubBannerText() to the
// local I2C API version, until the first real device status arrives --
// mirrors control.cpp's splashScreenPoll() external state machine,
// ZuluSCSI-firmware's SplashScreen.cpp is equally dumb) and SettingsScreen's
// "About" entry (banner set to the I2C API version, no sub-banner, no
// auto-advance -- shortUserPress() returns to Settings, matching
// SplashScreen.cpp:30-33's "About" round-trip).
//
// draw() only reserves room for a second line when one is actually set
// (init() resets it to "" on every screen switch, so About's single-line
// look is unaffected) -- with one line it's centered at the same y=56 the
// original single-banner layout always used; with two, the main banner
// moves up to y=48 and the sub-banner takes y=56, so the pair still ends
// flush with the bottom of the display exactly like the single line did.

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
    void setSubBannerText(const char *text);

private:
    char _bannerText[40] = "";
    char _subBannerText[40] = "";
};

}  // namespace zuluide::display
