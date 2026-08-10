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

// Port of ZuluSCSI-firmware's "SCSI Map" (lib/ZuluSCSI_UI_RP2MCU/
// MainScreen.h/.cpp), with one deliberate deviation: the original draws
// all MAX_DEVICES (16) SCSI-ID slots at their raw ID's fixed grid position
// (8 per page, 4 per column, MainScreen.cpp:32-33,109-120), showing an
// empty/off-LED entry for every unpopulated ID -- so gaps between present
// IDs show up as blank slots. Here, draw() instead compacts every present
// device (`.present`, this iteration's DeviceMap::Active stand-in) into
// consecutive grid slots in reading order -- upper-left down to
// lower-left, then upper-right down to lower-right -- continuing onto
// further DEVICES_PER_PAGE-sized pages as needed, so no slot is ever left
// blank between two present devices. Selection/rotaryChange still track
// the device's real SCSI-ID index (only ever landing on present slots,
// exactly like the original's Active-only search loops); draw() derives
// each page's slate and the current page number from the selected
// device's *rank* among present devices, not its raw ID.
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
    void shortUserPress() override;
    void shortEjectPress() override;
    void longUserPress() override;
    void longEjectPress() override;
    void rotaryChange(int direction) override;

    int SelectedDeviceIndex() const { return _selectedIndex; }

private:
    DisplayData *_data;
    int _selectedIndex = -1;

    void drawDeviceItem(int x, int y, int deviceIndex);

    // Ejects the currently-selected device's image (I2C_CLIENT_EJECT_IMAGE)
    // and pops a brief confirmation MessageBox. Shared by longEjectPress()
    // and the Eject-confirmation menu's "Eject" item.
    void ejectSelected();

    // Inserts media into the currently-selected device (I2C_CLIENT_INSERT_MEDIA),
    // reached from the eject menu's "Insert" item (offered only while ejected).
    void insertSelected();

    // Builds the Eject/(Insert)/Cancel confirmation menu into _ejItems/
    // _ejActions -- "Insert" is only present when the device is ejected, so the
    // per-row action is recorded rather than assumed by position.
    static constexpr int kMaxEjectItems = 3;
    const char *_ejItems[kMaxEjectItems] = {nullptr};
    int _ejActions[kMaxEjectItems] = {0};
    int _ejCount = 0;
    void buildEjectMenu();

    // MenuScreen activation callback (ctx == this) for the ZuluIDE-style
    // Eject confirmation the eject button now raises. Static so it can be
    // passed as a plain function pointer to ShowMenu.
    static void onMenuAction(void *ctx, int menuId, int selected);

    // Selects the first present device if nothing is selected yet -- called
    // from both init() and tick(), since the status JSON may not have
    // arrived at all when this screen is first shown (e.g. right after
    // boot), leaving _selectedIndex at -1 until a later tick sees real data.
    void selectFirstPresentIfNeeded();
};

}  // namespace zuluide::display
