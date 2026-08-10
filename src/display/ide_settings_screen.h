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

// Settings screen for both device types -- registered as
// DisplayScreenType::Settings for ZuluSCSI as well as ZuluIDE (see
// screen_registry.cpp). Two editable rows backed by ui_settings.h (RAM-only,
// no persisted store yet): the Browse scroll step (1/10/50) and the screen
// saver style; then three rows that open a page of their own: WiFi status,
// filename-cache Usage, and About. The rotary dial moves between rows (CW =
// down); the rotary push opens a popup listing every choice for the
// highlighted row (task spec) -- the scroll popup hides steps larger than the
// image count, the saver popup lists all styles -- or switches to that row's
// page. The user or eject button returns to Main, or closes an open popup.
//
// There are more rows than fit above the hint line, so they are drawn through
// a kVisibleRows-tall viewport starting at _top, with PopupMenu's up/down
// triangle indicators when the list continues past either edge.

#pragma once

#include "screen.h"
#include "display_data.h"

namespace zuluide::display {

class IDESettingsScreen : public Screen
{
public:
    IDESettingsScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Settings; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortUserPress() override;
    void shortEjectPress() override;
    void shortRotaryPress() override;
    void rotaryChange(int direction) override;

private:
    DisplayData *_data;
    // 0 = scroll step, 1 = screen saver, 2 = WiFi status, 3 = Usage, 4 = About.
    int _selected = 0;
    int _top = 0;  // first row of the visible viewport

    static constexpr int kRowCount = 5;
    static constexpr int kVisibleRows = 3;

    // Storage for the menu's dynamically-built item list (MenuScreen only
    // borrows the label array). Saver lists all styles (Random + 6), scroll
    // lists the steps that fit the image count.
    static constexpr int kMaxMenuItems = 8;
    const char *_menuItems[kMaxMenuItems] = {nullptr};
    int _menuValues[kMaxMenuItems] = {0};
    int _menuCount = 0;

    void openScrollMenu();
    void openSaverMenu();

    // Moves _top the minimum distance needed to keep _selected on screen.
    void clampScroll();

    // MenuScreen activation callback (ctx == this).
    static void onMenuAction(void *ctx, int menuId, int selected);
};

}  // namespace zuluide::display
