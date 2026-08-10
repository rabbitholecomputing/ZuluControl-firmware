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

// Read-only "Usage" page, reached from the Settings screen's "Usage" row (the
// row directly above "About") on both the ZuluSCSI and ZuluIDE UIs, since both
// share IDESettingsScreen. Shows how full the two filename caches are:
//
//   * the shared filenames JSON buffer in main.cpp -- bytes written across
//     every scsi id, out of FILENAMES_JSON_CACHE_SIZE (webui_data.h's
//     GetFilenamesCacheBytesUsed());
//   * DisplayData's combined filename index -- images indexed across every
//     scsi id, out of DisplayData::MAX_FILENAME_INDEX entries.
//
// Both are the caps a large SD card actually runs into first (see
// display_data.h's MAX_FILENAME_INDEX comment), which is what makes them worth
// surfacing. Any button returns to Settings; nothing here is editable.

#pragma once

#include "screen.h"
#include "display_data.h"

namespace zuluide::display {

class UsageScreen : public Screen
{
public:
    UsageScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Usage; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortUserPress() override;
    void shortEjectPress() override;
    void shortRotaryPress() override;

private:
    DisplayData *_data;

    void back();
};

}  // namespace zuluide::display
