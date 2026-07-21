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

// Hidden placeholder for every ZuluSCSI_UI_RP2MCU screen NOT ported this
// iteration (Settings, Info/InfoPage2-4, BrowseType, MessageBox, Copy,
// InitiatorMain, NoControlsError). One instance per DisplayScreenType is
// registered in screen_registry.cpp so the factory/enum shape is complete
// for the next iteration, but no navigation path in this iteration ever
// calls ChangeScreen() with one of these types -- draw() is intentionally
// blank.

#pragma once

#include "screen.h"

namespace zuluide::display {

class PlaceholderScreen : public Screen
{
public:
    PlaceholderScreen(Framebuffer128x64 *fb, DisplayScreenType type) : Screen(fb), _type(type) {}

    DisplayScreenType screenType() const override { return _type; }

    void draw() override {}

private:
    DisplayScreenType _type;
};

}  // namespace zuluide::display
