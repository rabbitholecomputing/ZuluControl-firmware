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

// ZuluIDE browse screen -- registered as DisplayScreenType::Browse when the
// connected device is a ZuluIDE. Same flat, cached-filenames list as the
// ZuluSCSI BrowseScreen (single implicit device, slot 0), but with the
// task-specified ZuluIDE button mapping and a configurable rotary scroll
// step (ui_settings.h's GetScrollStep(): 1/10/50 entries per detent):
//  - rotary dial            -> move the cursor by the current scroll step
//  - rotary button (push)   -> load the selected image
//  - eject button           -> back to Main, UNLESS the scroll step is > 1,
//                              in which case it resets the step to 1 and
//                              shows a brief confirmation instead of leaving
//  - user button (Insert)   -> opens the browse popup menu (Select / Close /
//                              Scroll 1 / Scroll 10 / Scroll 50)
// While the popup is open, the rotary dial moves the selection (CW = down),
// the rotary push activates it, and the eject button closes the popup.

#pragma once

#include "screen.h"
#include "display_data.h"
#include "../ZuluControl_config.h"

namespace zuluide::display {

class IDEBrowseScreen : public Screen
{
public:
    IDEBrowseScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Browse; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortUserPress() override;
    void shortEjectPress() override;
    void shortRotaryPress() override;
    void rotaryChange(int direction) override;

private:
    DisplayData *_data;
    int _cursor = 0;
    bool _ready = false;

    // Set on transitions that bounce straight back into this screen (closing
    // the menu, changing the scroll step, the eject scroll-reset), so init()
    // keeps the currently-browsed image instead of jumping back to the first.
    // A fresh entry from Main leaves it false and starts at the top. One-shot:
    // consumed (cleared) by init().
    bool _preserveCursor = false;

    // The browse menu is built per-open (the offered Scroll N options depend
    // on how many images the list holds -- see buildMenu()), so the label
    // pointers and their actions need storage that outlives the MenuScreen
    // (which only borrows the label array). Select + Close + Main Screen +
    // up to three scroll steps = 6 entries max.
    static constexpr int kMaxMenuItems = 6;
    const char *_menuItems[kMaxMenuItems] = {nullptr};
    int _menuActions[kMaxMenuItems] = {0};
    int _menuCount = 0;

    void buildMenu();

    // Ping-pong marquee state for the filename line (same timing as
    // InfoScreen's / the SCSI BrowseScreen's scroller).
    char _lastScrollName[MAX_FILE_PATH] = "";
    int _scrollOffsetPx = 0;
    bool _scrolling = false;
    bool _scrollReverse = false;
    uint32_t _scrollPauseUntilMs = 0;
    uint32_t _lastScrollStepMs = 0;

    void updateScroll(const char *filename);
    void loadSelected();

    // MenuScreen activation callback (ctx == this).
    static void onMenuAction(void *ctx, int menuId, int selected);
};

}  // namespace zuluide::display
