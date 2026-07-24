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

// Flat, JSON-list-backed browse screen -- see the plan's "Browsing is flat,
// not hierarchical" note: ZuluControl-firmware has no SD access, so unlike
// ZuluSCSI-firmware's BrowseScreen/BrowseScreenFlat (which walk the SD card
// live), this scrolls through the selected device's cached filenames list,
// fetched via I2C_CLIENT_FETCH_FILENAMES (the lighter per-device
// filenames_json cache, not the heavier per-file imageJson/
// I2C_CLIENT_FETCH_IMAGES_JSON one) -- no new protocol. The filename index
// (DisplayData::MAX_FILENAME_INDEX, shared across all scsi ids at once) is
// rebuilt automatically inside DisplayData::Refresh() whenever any
// device's cached JSON changes -- this screen doesn't trigger indexing
// itself, it just calls Refresh() like every other screen. draw()/
// rotaryChange()/shortRotaryPress() move `_cursor` and call
// DisplayData::GetIndexedFilename(_scsiId, _cursor, ...), which jumps
// straight to that one entry's bytes via the index -- so switching to the
// next/previous filename never re-parses the array. That source carries only paths, no
// size/isDir, so this screen doesn't show a size line. Header matches
// BrowseScreenFlat.cpp:184-200: large (2x) SCSI ID right-aligned with the
// device-type icon immediately to its left. Filename line is a scrolling
// marquee (see updateScroll()), identical timing to InfoScreen's -- not
// ellipsized like Screen's other default text.
//
// Task-specified button mapping:
//  - rotary dial            -> move the cursor by the current scroll step
//                              (ui_settings.h's GetScrollStep(): 1/10/50)
//  - rotary button (push)   -> load the selected image, then go to Info
//  - eject button           -> back to the Info screen
//  - user button (Insert)   -> open the browse popup menu -- like the ZuluIDE
//                              browse menu, but with a "Map" entry (go to the
//                              SCSI Map) added after "Close": Select / Close /
//                              Map / Scroll 1 / Scroll 10 / Scroll 50
// While the popup is open, the rotary dial moves the selection, the rotary
// push activates it, and the eject button closes the popup.

#pragma once

#include "screen.h"
#include "display_data.h"
#include "../ZuluControl_config.h"
#include <cstdint>

namespace zuluide::display {

class BrowseScreen : public Screen
{
public:
    BrowseScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Browse; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortUserPress() override;
    void shortRotaryPress() override;
    void shortEjectPress() override;
    void rotaryChange(int direction) override;

private:
    DisplayData *_data;
    int _scsiId = 0;
    int _cursor = 0;
    bool _ready = false;

    // Set on transitions that bounce straight back into this screen (closing
    // the menu, changing the scroll step), so init() keeps the currently-
    // browsed image instead of jumping back to the first. A fresh entry from
    // another screen leaves it false and starts at the top. One-shot: consumed
    // (cleared) by init().
    bool _preserveCursor = false;

    // The browse menu is built per-open (the offered Scroll N options depend on
    // how many images the list holds), so the label pointers and their actions
    // need storage that outlives the MenuScreen (which only borrows the label
    // array). Select + Close + Map + Info + up to three scroll steps = 7 max.
    static constexpr int kMaxMenuItems = 7;
    const char *_menuItems[kMaxMenuItems] = {nullptr};
    int _menuActions[kMaxMenuItems] = {0};
    int _menuCount = 0;

    void buildMenu();
    void loadSelected();

    // MenuScreen activation callback (ctx == this).
    static void onMenuAction(void *ctx, int menuId, int selected);

    // Ping-pong marquee state for the filename line, timed identically to
    // InfoScreen's scroller (see info_screen.cpp's updateScroll()): 3px
    // per 360ms step, 1000ms pause at each end before reversing. Keyed off
    // the filename text itself (not _cursor), so scrolling in tick()
    // naturally resets whenever the selection moves to a different entry.
    char _lastScrollName[MAX_FILE_PATH] = "";
    int _scrollOffsetPx = 0;
    bool _scrolling = false;
    bool _scrollReverse = false;
    uint32_t _scrollPauseUntilMs = 0;
    uint32_t _lastScrollStepMs = 0;

    void updateScroll(const char *filename);
};

}  // namespace zuluide::display
