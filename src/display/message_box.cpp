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

#include "message_box.h"
#include "screen_registry.h"
#include <pico/time.h>
#include <cstdio>

namespace zuluide::display {

namespace {
uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }

// Box bounds, same as MessageBox.cpp's Rectangle{{15,10},{98,44}}.
constexpr int kBoxX = 15, kBoxY = 10, kBoxW = 98, kBoxH = 44;
}  // namespace

void MessageBox::init(int index)
{
    Screen::init(index);
    _shownAtMs = millis();
    // No forceDraw()/clear here -- _hasDrawn is already false from
    // Screen::init(), and tick() below draws without clearing so the
    // screen behind this overlay stays visible.
}

void MessageBox::tick()
{
    if (_autoCloseMs > 0 && (millis() - _shownAtMs) >= _autoCloseMs)
    {
        dismiss();
        return;
    }

    if (!_hasDrawn)
    {
        draw();
        _hasDrawn = true;
    }
}

void MessageBox::draw()
{
    // NB: no _fb->clear() -- drawn directly over whatever's already there.
    _fb->fillRect(kBoxX, kBoxY, kBoxW, kBoxH, false);
    _fb->drawRectOutline(kBoxX + 2, kBoxY + 2, kBoxW - 4, kBoxH - 4);

    printCenteredText(_title, kBoxY + 6);
    _fb->drawHLine(kBoxX + 2, kBoxY + 15, kBoxW - 4);
    printCenteredText(_line1, kBoxY + 19);
    printCenteredText(_line2, kBoxY + 28);
}

void MessageBox::SetText(const char *title, const char *line1, const char *line2)
{
    snprintf(_title, sizeof(_title), "%s", title);
    snprintf(_line1, sizeof(_line1), "%s", line1);
    snprintf(_line2, sizeof(_line2), "%s", line2);
}

void MessageBox::SetReturnScreen(DisplayScreenType type, int index)
{
    _returnScreen = type;
    _returnIndex = index;
}

void MessageBox::SetAutoCloseMs(uint32_t ms)
{
    _autoCloseMs = ms;
}

void MessageBox::SetBlocking(bool blocking)
{
    _blocking = blocking;
}

void MessageBox::dismiss()
{
    ChangeScreen(_returnScreen, _returnIndex);
}

void MessageBox::shortRotaryPress()
{
    if (!_blocking) dismiss();
}

void MessageBox::shortUserPress()
{
    if (!_blocking) dismiss();
}

void MessageBox::shortEjectPress()
{
    if (!_blocking) dismiss();
}

void MessageBox::rotaryChange(int)
{
    if (!_blocking) dismiss();
}

}  // namespace zuluide::display
