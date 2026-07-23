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

// Reusable modal popup menu, drawn on top of whatever screen owns it (it
// clears only its own box, like MessageBox, so the screen behind stays
// visible around it). It is NOT a Screen subclass: the owning screen embeds
// one as a member, routes input to it while IsOpen(), and draws it last in
// its own draw(). This keeps each menu's "what does activating item N do"
// logic in the screen that owns it, instead of a central dispatch table --
// the ZuluIDE main and browse screens open different menus (and a couple of
// modes each), distinguished by the caller-chosen MenuId() tag.
//
// Navigation matches the task's spec for the ZuluIDE UI: rotary clockwise
// moves the selection DOWN the list, counter-clockwise moves UP (Move()),
// the rotary push activates the highlighted item (the owner reads
// Selected()), and the eject button closes the menu (the owner calls
// Close()).
//
// The selected item is marked with a right-pointing arrow at the left margin
// (icon_select), same as SettingsScreen. Lists taller than the box scroll to
// keep the selection visible, and small triangles in the box's top-right
// corner indicate more items above (up) and/or below (down) -- both, down
// drawn first, when the list extends in both directions. An optional larger
// text scale is supported for menus that want bigger, more legible entries.

#pragma once

#include "framebuffer.h"

namespace zuluide::display {

class PopupMenu
{
public:
    static constexpr int MAX_ITEMS = 8;

    // Opens the menu with `count` items (labels are borrowed, not copied --
    // pass string literals or storage that outlives the menu). `menuId` is an
    // opaque tag the owner uses to tell which menu is showing when an item is
    // activated. `textScale` blows the item text up (1 = normal, 2 = double
    // height/width). Selection starts at item 0.
    void Open(int menuId, const char *title, const char *const *items, int count, int textScale = 1);
    void Close();

    bool IsOpen() const { return _open; }
    int MenuId() const { return _menuId; }
    int Selected() const { return _selected; }

    // direction > 0 = rotary clockwise = move down; < 0 = up. Wraps around,
    // and scrolls the visible window so the new selection stays on screen.
    void Move(int direction);

    // Draws the menu box + items over the current framebuffer contents (no
    // clear of the whole screen -- only its own box, so it overlays).
    void Draw(Framebuffer128x64 *fb) const;

private:
    bool _open = false;
    int _menuId = 0;
    const char *_title = "";
    const char *const *_items = nullptr;
    int _count = 0;
    int _selected = 0;
    int _scrollTop = 0;   // index of the first visible item
    int _textScale = 1;

    // How many item rows fit in the box at the current text scale.
    int visibleRows() const;
    // Keeps _selected within [_scrollTop, _scrollTop + visibleRows()).
    void clampScroll();
};

}  // namespace zuluide::display
