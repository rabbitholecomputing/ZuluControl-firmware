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

#include "popup_menu.h"
#include "icons.h"

namespace zuluide::display {

namespace {
// Box + list geometry. The box spans nearly the full width; the item area
// runs from kFirstRowY down to kItemAreaBottom. Row height scales with the
// text so bigger text simply shows fewer rows and scrolls.
constexpr int kBoxX = 4;
constexpr int kBoxW = Framebuffer128x64::WIDTH - 2 * kBoxX;  // 120
constexpr int kTitleY = 2;
constexpr int kDividerY = 11;
constexpr int kFirstRowY = 14;
constexpr int kItemAreaBottom = 62;
constexpr int kGlyphH = 7;   // font5x7 glyph height (per scale unit)
constexpr int kArrowW = 8;   // icon_select is 8x8
constexpr int kTextInsetX = 12;  // room for the selection arrow + gap

int rowHeightFor(int scale)
{
    return 8 * scale;  // scale 1 -> 8px rows, scale 2 -> 16px rows
}

// Small filled triangle used for the scroll indicators. `down` true points
// down (widest at top), false points up (widest at bottom). w should be odd.
void drawTriangle(Framebuffer128x64 *fb, int x, int y, int w, int h, bool down)
{
    for (int r = 0; r < h; r++)
    {
        int shrink = down ? r : (h - 1 - r);
        int lineW = w - 2 * shrink;
        if (lineW > 0)
            fb->drawHLine(x + shrink, y + r, lineW);
    }
}
}  // namespace

void PopupMenu::Open(int menuId, const char *title, const char *const *items, int count, int textScale)
{
    _menuId = menuId;
    _title = title ? title : "";
    _items = items;
    _count = count > MAX_ITEMS ? MAX_ITEMS : count;
    _selected = 0;
    _scrollTop = 0;
    _textScale = textScale < 1 ? 1 : textScale;
    _open = true;
}

void PopupMenu::Close()
{
    _open = false;
}

int PopupMenu::visibleRows() const
{
    int rowH = rowHeightFor(_textScale);
    int rows = (kItemAreaBottom - kFirstRowY) / rowH;
    if (rows < 1)
        rows = 1;
    return rows;
}

void PopupMenu::clampScroll()
{
    int rows = visibleRows();
    if (_selected < _scrollTop)
        _scrollTop = _selected;
    else if (_selected >= _scrollTop + rows)
        _scrollTop = _selected - rows + 1;
    if (_scrollTop < 0)
        _scrollTop = 0;
}

void PopupMenu::Move(int direction)
{
    if (!_open || _count <= 0)
        return;
    if (direction > 0)
        _selected = (_selected + 1) % _count;
    else if (direction < 0)
        _selected = (_selected - 1 + _count) % _count;
    clampScroll();
}

void PopupMenu::Draw(Framebuffer128x64 *fb) const
{
    if (!_open)
        return;

    int rowH = rowHeightFor(_textScale);
    int maxRows = visibleRows();
    int rowsShown = _count < maxRows ? _count : maxRows;

    int h = kFirstRowY + rowsShown * rowH + 2;
    if (h > Framebuffer128x64::HEIGHT)
        h = Framebuffer128x64::HEIGHT;
    int y = (Framebuffer128x64::HEIGHT - h) / 2;

    // Clear behind the box, then outline it -- overlay, same as MessageBox.
    fb->fillRect(kBoxX, y, kBoxW, h, false);
    fb->drawRectOutline(kBoxX, y, kBoxW, h, true);

    // Title (always normal size), with a divider under it.
    int titleW = Framebuffer128x64::textWidth(_title);
    int titleX = kBoxX + (kBoxW - titleW) / 2;
    if (titleX < kBoxX + 2) titleX = kBoxX + 2;
    fb->drawText(titleX, y + kTitleY, _title);
    fb->drawHLine(kBoxX + 2, y + kDividerY, kBoxW - 4);

    // Scroll indicators in the box's top-right corner: down = more below,
    // up = more above; both (down first) when the list extends both ways.
    bool moreAbove = _scrollTop > 0;
    bool moreBelow = _scrollTop + rowsShown < _count;
    constexpr int kTriW = 7, kTriH = 4;
    int triY = y + 2;
    int triRight = kBoxX + kBoxW - 3;
    if (moreBelow && moreAbove)
    {
        drawTriangle(fb, triRight - (2 * kTriW + 2), triY, kTriW, kTriH, true);
        drawTriangle(fb, triRight - kTriW, triY, kTriW, kTriH, false);
    }
    else if (moreBelow)
    {
        drawTriangle(fb, triRight - kTriW, triY, kTriW, kTriH, true);
    }
    else if (moreAbove)
    {
        drawTriangle(fb, triRight - kTriW, triY, kTriW, kTriH, false);
    }

    int glyphH = kGlyphH * _textScale;
    int textYOff = (rowH - glyphH) / 2;
    if (textYOff < 0) textYOff = 0;

    for (int row = 0; row < rowsShown; row++)
    {
        int idx = _scrollTop + row;
        if (idx >= _count)
            break;
        int rowTop = y + kFirstRowY + row * rowH;

        // Selected item marked with a right-pointing arrow at the left margin
        // (vertically centered in the row), same as SettingsScreen.
        if (idx == _selected)
        {
            int arrowY = rowTop + (rowH - kArrowW) / 2;
            if (arrowY < rowTop) arrowY = rowTop;
            fb->drawBitmap(kBoxX + 2, arrowY, icon_select, kArrowW, kArrowW);
        }
        fb->drawText(kBoxX + kTextInsetX, rowTop + textYOff, _items[idx], true, _textScale);
    }
}

}  // namespace zuluide::display
