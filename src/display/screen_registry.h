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

// Screen factory + active-screen transition, port of ZuluSCSI-firmware's
// lib/ZuluSCSI_UI_RP2MCU/control_global.h/.cpp (GetScreen()/changeScreen()).
// Every DisplayScreenType value is registered (Splash/Main/Browse against
// their real implementations, everything else against PlaceholderScreen)
// so the enum/factory shape is complete for the next iteration.

#pragma once

#include "screen_type.h"
#include "screen.h"
#include "framebuffer.h"
#include "display_data.h"

namespace zuluide::display {

// Allocates one instance of every screen. Call once at startup before any
// ChangeScreen()/GetScreen() call.
void InitScreens(Framebuffer128x64 *fb, DisplayData *data);

Screen *GetScreen(DisplayScreenType type);

// Switches the active screen, calling its init(index). index == -1
// resolves to whatever index the previous ChangeScreen() call used (mirrors
// control_global.cpp's SCREEN_ID_NO_PREVIOUS/-1 handling).
void ChangeScreen(DisplayScreenType type, int index = -1);

Screen *GetActiveScreen();

// Configures the shared MessageBox instance and switches to it -- the
// normal way other screens/display_task pop a modal notification, rather
// than reaching into MessageBox directly. `returnScreen`/`returnIndex` are
// where dismissing (any button, or `autoCloseMs` elapsing if non-zero)
// switches back to.
void ShowMessage(const char *title, const char *line1, const char *line2,
                  DisplayScreenType returnScreen, int returnIndex, uint32_t autoCloseMs = 0);

}  // namespace zuluide::display
