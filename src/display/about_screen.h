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

// About screen for the ZuluIDE UI (DisplayScreenType::About) -- reached from
// the main popup menu's "About" entry. Draws the logo (by device type, same
// as SplashScreen) plus the I2C API version banner, and returns to Main on
// ANY button/rotary input (task spec). Distinct from SplashScreen, whose
// shortUserPress() opens Settings and which the boot sequence drives.

#pragma once

#include "screen.h"

namespace zuluide::display {

class AboutScreen : public Screen
{
public:
    explicit AboutScreen(Framebuffer128x64 *fb) : Screen(fb) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::About; }

    void init(int index) override;
    void draw() override;

    void shortUserPress() override;
    void shortEjectPress() override;
    void shortRotaryPress() override;
    void rotaryChange(int direction) override;

private:
    void dismiss();
};

}  // namespace zuluide::display
