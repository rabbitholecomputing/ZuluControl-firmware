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

// A scrollable list menu that is its own full screen (DisplayScreenType::Menu),
// not an overlay embedded in another screen. A single shared instance is
// configured and switched to via ShowMenu() -- exactly the pattern MessageBox
// uses through ShowMessage() -- so any screen can raise a menu without owning
// one. It reuses PopupMenu purely for the list state + rendering (selection,
// scrolling window, the more-items triangles, the selection arrow).
//
// Navigation: rotary CW/CCW moves the selection, the rotary push activates the
// highlighted item, and the user or eject button closes the menu (returns to
// the configured return screen). On activation the configured callback runs;
// if it navigates to another screen the menu stays out of the way, otherwise
// the menu falls back to the return screen (so "Close"/value-set items land
// back where they came from without every callback repeating that).

#pragma once

#include "screen.h"
#include "popup_menu.h"

namespace zuluide::display {

// Invoked when the user activates a menu item. `ctx` is the opaque pointer
// handed to ShowMenu (typically the screen that opened the menu, letting the
// callback -- usually that screen's static method -- reach its own state),
// `menuId` distinguishes which menu, `selected` is the chosen item index.
using MenuActionFn = void (*)(void *ctx, int menuId, int selected);

class MenuScreen : public Screen
{
public:
    explicit MenuScreen(Framebuffer128x64 *fb) : Screen(fb) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Menu; }

    // Stashes the menu definition; the actual PopupMenu::Open() happens in
    // init() (i.e. when ChangeScreen switches to this screen). `items` is
    // borrowed, not copied -- it must outlive the menu (string literals or
    // storage owned by the caller are fine). `ejectSelectsIndex`, when >= 0,
    // makes the physical eject button activate that item (e.g. the "Eject"
    // entry of a confirmation menu) instead of just closing the menu; -1
    // (the default) keeps eject as a plain close.
    void Configure(int menuId, const char *title, const char *const *items, int count,
                    int textScale, DisplayScreenType returnScreen, int returnIndex,
                    MenuActionFn onSelect, void *ctx, int ejectSelectsIndex = -1);

    void init(int index) override;
    void draw() override;

    void rotaryChange(int direction) override;
    void shortRotaryPress() override;
    void shortEjectPress() override;
    void shortUserPress() override;

private:
    PopupMenu _menu;
    int _menuId = 0;
    const char *_title = "";
    const char *const *_items = nullptr;
    int _count = 0;
    int _textScale = 1;
    DisplayScreenType _returnScreen = DisplayScreenType::Main;
    int _returnIndex = -1;
    MenuActionFn _onSelect = nullptr;
    void *_ctx = nullptr;
    int _ejectSelectsIndex = -1;

    // Runs _onSelect for item `index`, then falls back to the return screen
    // if the callback didn't itself navigate away.
    void activate(int index);
    void closeToReturn();
};

// Configures the shared MenuScreen and switches to it. See MenuScreen above
// for the callback / return-screen contract and `ejectSelectsIndex`.
void ShowMenu(int menuId, const char *title, const char *const *items, int count,
              int textScale, DisplayScreenType returnScreen, int returnIndex,
              MenuActionFn onSelect, void *ctx, int ejectSelectsIndex = -1);

}  // namespace zuluide::display
