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

#include "menu_screen.h"
#include "screen_registry.h"

namespace zuluide::display {

void MenuScreen::Configure(int menuId, const char *title, const char *const *items, int count,
                            int textScale, DisplayScreenType returnScreen, int returnIndex,
                            MenuActionFn onSelect, void *ctx, int ejectSelectsIndex)
{
    _menuId = menuId;
    _title = title ? title : "";
    _items = items;
    _count = count;
    _textScale = textScale;
    _returnScreen = returnScreen;
    _returnIndex = returnIndex;
    _onSelect = onSelect;
    _ctx = ctx;
    _ejectSelectsIndex = ejectSelectsIndex;
}

void MenuScreen::init(int index)
{
    Screen::init(index);
    _menu.Open(_menuId, _title, _items, _count, _textScale);
    forceDraw();
}

void MenuScreen::draw()
{
    // Its own screen now, so the framebuffer is already cleared by
    // Screen::tick() -- the menu box draws onto a blank background rather than
    // overlaying another screen.
    _menu.Draw(_fb);
}

void MenuScreen::closeToReturn()
{
    ChangeScreen(_returnScreen, _returnIndex);
}

void MenuScreen::activate(int index)
{
    // Run the caller's action. If it switched to another screen, leave it
    // there; otherwise fall back to the return screen so "Close"/value-set
    // items don't each have to navigate themselves.
    Screen *self = GetActiveScreen();
    if (_onSelect)
        _onSelect(_ctx, _menuId, index);
    if (GetActiveScreen() == self)
        closeToReturn();
}

void MenuScreen::rotaryChange(int direction)
{
    _menu.Move(direction);
    forceDraw();
}

void MenuScreen::shortRotaryPress()
{
    activate(_menu.Selected());
}

void MenuScreen::shortEjectPress()
{
    // Eject either activates a designated item (e.g. the "Eject" entry of a
    // confirmation menu) or, by default, just closes the menu.
    if (_ejectSelectsIndex >= 0)
        activate(_ejectSelectsIndex);
    else
        closeToReturn();
}

void MenuScreen::shortUserPress()
{
    closeToReturn();
}

void ShowMenu(int menuId, const char *title, const char *const *items, int count,
              int textScale, DisplayScreenType returnScreen, int returnIndex,
              MenuActionFn onSelect, void *ctx, int ejectSelectsIndex)
{
    auto *menu = static_cast<MenuScreen *>(GetScreen(DisplayScreenType::Menu));
    if (!menu)
        return;
    menu->Configure(menuId, title, items, count, textScale, returnScreen, returnIndex,
                    onSelect, ctx, ejectSelectsIndex);
    ChangeScreen(DisplayScreenType::Menu, returnIndex);
}

}  // namespace zuluide::display
