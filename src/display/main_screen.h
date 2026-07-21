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

// Full port of ZuluSCSI-firmware's "SCSI Map" (lib/ZuluSCSI_UI_RP2MCU/
// MainScreen.h/.cpp): all MAX_DEVICES (16) SCSI-ID slots are drawn (8 per
// page, 4 per column, MainScreen.cpp:32-33,109-120), not just the ones a
// device is actually present at -- DisplayData::GetDevice() always
// returns a valid slot; `.present` (this iteration's DeviceMap::Active
// stand-in) decides whether the type icon/LED are drawn for it, same as
// the original's `if (map->Active)` branch. Selection/rotaryChange only
// ever lands on present slots, exactly like the original's Active-only
// search loops.
//
// Unlike the original (always 2 fixed pages for 16 targets), the page
// count here is derived from the highest present device's slot -- e.g.
// "(1/1)" rather than a fixed "(1/2)" when nothing is present above SCSI
// ID 7, since our data comes from a JSON device list rather than a live
// SD-card scan of all 16 IDs.
//
// The status JSON can arrive after this screen is already showing (e.g.
// right at boot, before the first I2C round trip completes), which would
// otherwise leave the cursor permanently unselected -- selectFirstPresentIfNeeded()
// re-tries every tick() until something is selectable.
//
// LED state uses three icons instead of the original's two: icon_ledon
// (present, media loaded), icon_ledsemi (present, ejected/empty), icon_ledoff
// (slot not present) -- a reasonable extra use of the `ejected` field this
// iteration's JSON actually carries, in place of the original's simpler
// Active-only on/off.
//
// Hidden placeholders (not drawn -- the JSON doesn't carry these fields
// yet): IsRom/IsRaw overlay icons.
//
// shortRotaryPress() opens the real Info screen for the selected device.
// longUserPress() goes to the real Settings screen. longEjectPress()
// directly ejects the selected device's image (I2C_CLIENT_EJECT_IMAGE), a
// real action the existing protocol supports without needing a
// browse-type chooser screen -- confirmed via a MessageBox popup.

#pragma once

#include "screen.h"
#include "display_data.h"

namespace zuluide::display {

class MainScreen : public Screen
{
public:
    MainScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Main; }

    void init(int index) override;
    void draw() override;
    void tick() override;

    void shortRotaryPress() override;
    void shortEjectPress() override;
    void longUserPress() override;
    void longEjectPress() override;
    void rotaryChange(int direction) override;

    int SelectedDeviceIndex() const { return _selectedIndex; }

private:
    DisplayData *_data;
    int _selectedIndex = -1;

    void drawDeviceItem(int x, int y, int deviceIndex);

    // Selects the first present device if nothing is selected yet -- called
    // from both init() and tick(), since the status JSON may not have
    // arrived at all when this screen is first shown (e.g. right after
    // boot), leaving _selectedIndex at -1 until a later tick sees real data.
    void selectFirstPresentIfNeeded();
};

}  // namespace zuluide::display
