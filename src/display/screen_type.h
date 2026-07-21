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

// Mirrors ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/ScreenType.h so the
// factory/registry shape is complete for the next iteration. Splash/Main/
// Browse are implemented this iteration (screen_registry.cpp); every other
// value is registered against a stub PlaceholderScreen (placeholder_screens.h)
// that no navigation path in this iteration ever reaches.

#pragma once

namespace zuluide::display {

enum class DisplayScreenType
{
    None,
    Splash,
    Settings,
    Main,
    Info,
    InfoPage2,
    InfoPage3,
    InfoPage4,
    BrowseType,
    Browse,
    MessageBox,
    Copy,
    InitiatorMain,
    NoControlsError,
};

}  // namespace zuluide::display
