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

// ZuluIDE main screen -- registered as DisplayScreenType::Main when the
// connected device is a ZuluIDE (screen_registry.cpp branches on
// g_device_type; the ZuluSCSI build keeps using MainScreen's SCSI-map grid).
// Instead of listing every device like the SCSI map, it shows the single IDE
// device and its loaded image directly (essentially the Info screen promoted
// to the home screen, per the task): a media-type icon + "pri"/"sec" (the
// status JSON's isPrimary flag) + SD indicator in the header, the loaded
// image filename as a scrolling marquee (same timing as InfoScreen's), and a
// "Folder:" line resolved from the cached filenames list.
//
// Button mapping (task-specified, different from the SCSI UI):
//  - user button (Insert)  -> opens the main popup menu (Select / Eject /
//                             Close / Screensaver / Settings / About)
//  - eject button (Eject)  -> opens a Yes/No eject-confirmation popup first,
//                             rather than ejecting immediately
//  - rotary button (push)  -> opens the Browse screen
// While a popup is open, the rotary dial moves the selection (CW = down),
// the rotary push activates it, and the eject button closes the popup.

#pragma once

#include "screen.h"
#include "display_data.h"
#include "../ZuluControl_config.h"

namespace zuluide::display {

class IDEMainScreen : public Screen
{
public:
    IDEMainScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Main; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortUserPress() override;
    void shortEjectPress() override;
    void shortRotaryPress() override;
    void rotaryChange(int direction) override;

private:
    DisplayData *_data;

    // Ping-pong marquee state for the image filename, timed identically to
    // InfoScreen's scroller (see info_screen.cpp).
    char _lastImageName[MAX_FILE_PATH] = "";
    int _scrollOffsetPx = 0;
    bool _scrolling = false;
    bool _scrollReverse = false;
    uint32_t _scrollPauseUntilMs = 0;
    uint32_t _lastScrollStepMs = 0;

    void updateScroll(const char *filename);
    void ejectCurrent();

    // MenuScreen activation callback (ctx == this). Static so it can be passed
    // as a plain function pointer to ShowMenu while still reaching this
    // instance's state.
    static void onMenuAction(void *ctx, int menuId, int selected);
};

}  // namespace zuluide::display
