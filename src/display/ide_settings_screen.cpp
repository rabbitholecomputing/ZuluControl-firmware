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

enum { kRowScroll = 0, kRowSaver = 1, kRowWiFi = 2 };

// Menu tags passed to ShowMenu(), distinguished in onMenuAction().
enum { kMenuScroll = 0, kMenuSaver = 1 };
constexpr int kMenuScale = 1;
}  // namespace

void IDESettingsScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();
    _selected = 0;
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

    char line[28];

    snprintf(line, sizeof(line), "Scroll: %d", GetScrollStep());
    if (_selected == kRowScroll)
        _fb->drawBitmap(2, kRow0Y, icon_select, 8, 8);
    _fb->drawText(kTextX, kRow0Y, line);

    snprintf(line, sizeof(line), "Saver: %s", ScreenSaverName(GetConfiguredScreenSaver()));
    if (_selected == kRowSaver)
        _fb->drawBitmap(2, kRow0Y + kRowStep, icon_select, 8, 8);
    _fb->drawText(kTextX, kRow0Y + kRowStep, line);

    if (_selected == kRowWiFi)
        _fb->drawBitmap(2, kRow0Y + kRowStep * 2, icon_select, 8, 8);
    _fb->drawText(kTextX, kRow0Y + kRowStep * 2, "WiFi");

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
}

void IDESettingsScreen::rotaryChange(int direction)
{
    if (direction > 0)
        _selected = (_selected + 1) % kRowCount;
    else if (direction < 0)
        _selected = (_selected - 1 + kRowCount) % kRowCount;
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
