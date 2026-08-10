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

#include "ide_settings_screen.h"
#include "screen_registry.h"
#include "menu_screen.h"
#include "ui_settings.h"
#include "icons.h"
#include <cstdio>

namespace zuluide::display {

namespace {
constexpr int kRow0Y = 16;
constexpr int kRowStep = 12;
constexpr int kTextX = 12;

// Row order -- "Usage" sits directly above "About", the last row.
enum { kRowScroll = 0, kRowSaver = 1, kRowWiFi = 2, kRowUsage = 3, kRowAbout = 4 };

// Menu tags passed to ShowMenu(), distinguished in onMenuAction().
enum { kMenuScroll = 0, kMenuSaver = 1 };
constexpr int kMenuScale = 1;

// Scroll indicators for the row viewport, in the same visual language as
// PopupMenu's (popup_menu.cpp): a small filled triangle pointing the way the
// list continues. `down` true points down (widest at top).
constexpr int kTriW = 7, kTriH = 4;
constexpr int kTriX = Framebuffer128x64::WIDTH - kTriW - 2;

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

void IDESettingsScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();
    _selected = 0;
    _top = 0;
    forceDraw();
}

void IDESettingsScreen::tick()
{
    _data->Refresh();
    forceDraw();
    Screen::tick();
}

void IDESettingsScreen::draw()
{
    _fb->drawText(0, 0, "Settings");
    _fb->drawHLine(0, 10, 112);

    if (_data->SdPresent())
        _fb->drawBitmap(115, 0, icon_sd, 12, 12);
    else
        _fb->drawBitmap(115, 0, icon_nosd, 12, 12);

    // More rows than fit the viewport, so only kVisibleRows of them are drawn,
    // starting at _top (kept covering _selected by clampScroll()).
    char scrollLine[28], saverLine[28];
    snprintf(scrollLine, sizeof(scrollLine), "Scroll: %d", GetScrollStep());
    snprintf(saverLine, sizeof(saverLine), "Saver: %s", ScreenSaverName(GetConfiguredScreenSaver()));

    const char *labels[kRowCount];
    labels[kRowScroll] = scrollLine;
    labels[kRowSaver] = saverLine;
    labels[kRowWiFi] = "WiFi";
    labels[kRowUsage] = "Usage";
    labels[kRowAbout] = "About";

    for (int row = 0; row < kVisibleRows; row++)
    {
        int idx = _top + row;
        if (idx >= kRowCount)
            break;
        int y = kRow0Y + row * kRowStep;
        if (idx == _selected)
            _fb->drawBitmap(2, y, icon_select, 8, 8);
        _fb->drawText(kTextX, y, labels[idx]);
    }

    if (_top > 0)
        drawTriangle(_fb, kTriX, kRow0Y + 1, kTriW, kTriH, false);
    if (_top + kVisibleRows < kRowCount)
        drawTriangle(_fb, kTriX, kRow0Y + kRowStep * (kVisibleRows - 1) + 2, kTriW, kTriH, true);

    _fb->drawText(0, 52, "push:edit  usr:back");
}

void IDESettingsScreen::openScrollMenu()
{
    // Task: the Settings scroll menu always offers all three steps (1, 10, 50),
    // regardless of the current image count.
    static const int kSteps[] = {1, 10, 50};
    static const char *const kStepLabels[] = {"1", "10", "50"};
    int n = 0;
    for (int i = 0; i < 3; i++)
    {
        _menuItems[n] = kStepLabels[i];
        _menuValues[n] = kSteps[i];
        n++;
    }
    _menuCount = n;
    ShowMenu(kMenuScroll, "Scroll", _menuItems, _menuCount, kMenuScale,
             DisplayScreenType::Settings, getOriginalIndex(), &IDESettingsScreen::onMenuAction, this);
}

void IDESettingsScreen::openSaverMenu()
{
    // Every style: Random plus the 6 concrete ones (enum values 0..6).
    int n = 0;
    for (int i = 0; i <= TOTAL_SCREEN_SAVER_STYLES; i++)
    {
        _menuItems[n] = ScreenSaverName((ScreenSaverType)i);
        _menuValues[n] = i;
        n++;
    }
    _menuCount = n;
    ShowMenu(kMenuSaver, "Saver", _menuItems, _menuCount, kMenuScale,
             DisplayScreenType::Settings, getOriginalIndex(), &IDESettingsScreen::onMenuAction, this);
}

void IDESettingsScreen::onMenuAction(void *ctx, int menuId, int selected)
{
    auto *self = static_cast<IDESettingsScreen *>(ctx);
    int value = (selected >= 0 && selected < self->_menuCount) ? self->_menuValues[selected] : -1;

    if (value >= 0)
    {
        if (menuId == kMenuScroll)
            SetScrollStep(value);
        else if (menuId == kMenuSaver)
            SetConfiguredScreenSaver((ScreenSaverType)value);
    }
    // No navigation -> MenuScreen returns to Settings.
}

void IDESettingsScreen::shortRotaryPress()
{
    // Open the menu listing every choice for the highlighted row, or -- for the
    // WiFi row -- switch to the read-only WiFi status page.
    if (_selected == kRowScroll)
        openScrollMenu();
    else if (_selected == kRowSaver)
        openSaverMenu();
    else if (_selected == kRowWiFi)
        ChangeScreen(DisplayScreenType::WiFi, getOriginalIndex());
    else if (_selected == kRowUsage)
        ChangeScreen(DisplayScreenType::Usage, getOriginalIndex());
    else if (_selected == kRowAbout)
        ChangeScreen(DisplayScreenType::About, getOriginalIndex());
}

void IDESettingsScreen::clampScroll()
{
    // Same rule as PopupMenu::clampScroll(): pull the viewport just far enough
    // to cover _selected, which also lands it correctly after a wrap-around.
    if (_selected < _top)
        _top = _selected;
    else if (_selected >= _top + kVisibleRows)
        _top = _selected - kVisibleRows + 1;
    if (_top < 0)
        _top = 0;
}

void IDESettingsScreen::rotaryChange(int direction)
{
    if (direction > 0)
        _selected = (_selected + 1) % kRowCount;
    else if (direction < 0)
        _selected = (_selected - 1 + kRowCount) % kRowCount;
    clampScroll();
    forceDraw();
}

void IDESettingsScreen::shortUserPress()
{
    ChangeScreen(DisplayScreenType::Main, -1);
}

void IDESettingsScreen::shortEjectPress()
{
    ChangeScreen(DisplayScreenType::Main, -1);
}

}  // namespace zuluide::display
