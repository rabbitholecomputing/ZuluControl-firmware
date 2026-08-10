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

#include "ide_main_screen.h"
#include "screen_registry.h"
#include "menu_screen.h"
#include "ui_settings.h"
#include "icons.h"
#include "../ZuluControlI2CClient.h"
#include <pico/time.h>
#include <cstdio>
#include <cstring>

namespace zuluide::display {

namespace {
// Marquee geometry/timing -- identical to InfoScreen's filename scroller.
constexpr int kScrollX = 0;
constexpr int kScrollY = 16;
constexpr int kScrollW = Framebuffer128x64::WIDTH - kScrollX;
constexpr int kScrollStepPx = 3;
constexpr uint32_t kScrollIntervalMs = 360;
constexpr uint32_t kScrollPauseMs = 1000;

// IDE has a single implicit device, always at slot 0 (see
// DisplayData::parseStatus()).
constexpr int kIdeDeviceIndex = 0;

// Which MenuScreen we raised, so onMenuAction() can tell them apart.
enum { kMenuMain = 0, kMenuEjectConfirm = 1 };

// Menu labels -- borrowed by MenuScreen, so they must outlive it (static).
const char *const kMainMenuItems[] = {
    "Browse", "Eject", "Close", "Screensaver", "Settings", "About"
};
constexpr int kMainMenuCount = (int)(sizeof(kMainMenuItems) / sizeof(kMainMenuItems[0]));
enum { kMainBrowse = 0, kMainEject, kMainClose, kMainScreensaver, kMainSettings, kMainAbout };

// Eject first (default selection), Cancel second -- per the task spec.
const char *const kEjectConfirmItems[] = { "Eject", "Cancel" };
constexpr int kEjectConfirmCount = (int)(sizeof(kEjectConfirmItems) / sizeof(kEjectConfirmItems[0]));
enum { kEjectConfirm = 0, kEjectCancel = 1 };

// Normal-size menu text.
constexpr int kMenuScale = 1;

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }
}  // namespace

void IDEMainScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();

    _lastImageName[0] = '\0';
    _scrollOffsetPx = 0;
    _scrolling = false;
    _scrollReverse = false;

    forceDraw();
}

void IDEMainScreen::updateScroll(const char *filename)
{
    uint32_t now = millis();

    if (strcmp(filename, _lastImageName) != 0)
    {
        snprintf(_lastImageName, sizeof(_lastImageName), "%s", filename);
        _scrollOffsetPx = 0;
        _scrolling = false;
        _scrollReverse = false;
        _scrollPauseUntilMs = now + kScrollPauseMs;
        return;
    }

    int textW = Framebuffer128x64::textWidth(filename);
    int overflow = textW - kScrollW;
    if (overflow <= 0)
    {
        _scrollOffsetPx = 0;
        return;
    }

    if (!_scrolling)
    {
        if ((int32_t)(now - _scrollPauseUntilMs) < 0)
            return;
        _scrolling = true;
        _lastScrollStepMs = now;
    }

    if ((int32_t)(now - _lastScrollStepMs) < (int32_t)kScrollIntervalMs)
        return;
    _lastScrollStepMs = now;

    if (_scrollReverse)
    {
        _scrollOffsetPx -= kScrollStepPx;
        if (_scrollOffsetPx <= 0)
        {
            _scrollOffsetPx = 0;
            _scrolling = false;
            _scrollReverse = false;
            _scrollPauseUntilMs = now + kScrollPauseMs;
        }
    }
    else
    {
        _scrollOffsetPx += kScrollStepPx;
        if (_scrollOffsetPx >= overflow)
        {
            _scrollOffsetPx = overflow;
            _scrolling = false;
            _scrollReverse = true;
            _scrollPauseUntilMs = now + kScrollPauseMs;
        }
    }
}

void IDEMainScreen::tick()
{
    _data->Refresh();

    const DeviceInfo *dev = _data->GetDevice(kIdeDeviceIndex);
    if (dev)
        updateScroll(dev->image[0] != '\0' ? dev->image : "");

    forceDraw();
    Screen::tick();
}

void IDEMainScreen::draw()
{
    const DeviceInfo *dev = _data->GetDevice(kIdeDeviceIndex);

    // Header: media-type icon, "ZuluIDE" label, "pri"/"sec", SD indicator.
    const char *icon_type = (dev && dev->present) ? dev->type : "ZuluIDE";
    _fb->drawBitmap(0, 0, IconForIdeImageType(icon_type), 12, 12);
    _fb->drawText(15, 2, "ZuluIDE");

    if (dev && dev->present)
    {
        const char *role = dev->primary ? "pri" : "sec";
        // Sits just left of the SD icon (which occupies x=115..126).
        printRightAligned(role, 113, 2);
    }

    if (_data->SdPresent())
        _fb->drawBitmap(115, 0, icon_sd, 12, 12);
    else
        _fb->drawBitmap(115, 0, icon_nosd, 12, 12);

    _fb->drawHLine(0, 12, Framebuffer128x64::WIDTH);

    if (!dev || !dev->present)
    {
        printCenteredText("No device", 28);
    }
    else if (dev->image[0] == '\0')
    {
        // Ejected == no image loaded on ZuluIDE -- surface that state
        // explicitly rather than leaving the line blank.
        printCenteredText("-- Ejected --", 16);
    }
    else
    {
        // Scrolling marquee -- advanced once per tick() in updateScroll().
        _fb->setClip(kScrollX, kScrollY, kScrollW, 8);
        _fb->drawText(kScrollX - _scrollOffsetPx, kScrollY, dev->image);
        _fb->clearClip();
    }

    // A deferred load is waiting on the host to release/eject the current media;
    // the currently-loaded image (above) stays shown while this banner explains
    // why the newly-selected image hasn't taken effect yet.
    if (dev && dev->present && dev->deferred)
        printCenteredText("[Host deferred eject]", 32);

    // Hint line for the button mapping.
    _fb->drawText(0, 52, "push:browse  usr:menu");
}

void IDEMainScreen::ejectCurrent()
{
    zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_EJECT_IMAGE);
    // No image name on the second line -- just the action.
    ShowMessage("-- Info --", "Ejecting...", "",
                DisplayScreenType::Main, kIdeDeviceIndex, 1000);
}

void IDEMainScreen::onMenuAction(void *ctx, int menuId, int selected)
{
    auto *self = static_cast<IDEMainScreen *>(ctx);

    if (menuId == kMenuMain)
    {
        switch (selected)
        {
            // Items that navigate elsewhere; the rest fall through and let
            // MenuScreen return to Main on its own (see its shortRotaryPress).
            case kMainBrowse:      ChangeScreen(DisplayScreenType::Browse, kIdeDeviceIndex); return;
            case kMainEject:       self->ejectCurrent(); return;
            case kMainScreensaver: RequestScreenSaverNow(); return;
            case kMainSettings:    ChangeScreen(DisplayScreenType::Settings, kIdeDeviceIndex); return;
            case kMainAbout:       ChangeScreen(DisplayScreenType::About, kIdeDeviceIndex); return;
            case kMainClose:
            default:               return;
        }
    }
    else if (menuId == kMenuEjectConfirm)
    {
        if (selected == kEjectConfirm)
            self->ejectCurrent();
        // Cancel: do nothing -> MenuScreen returns to Main.
    }
}

void IDEMainScreen::shortUserPress()
{
    ShowMenu(kMenuMain, "Menu", kMainMenuItems, kMainMenuCount, kMenuScale,
             DisplayScreenType::Main, kIdeDeviceIndex, &IDEMainScreen::onMenuAction, this);
}

void IDEMainScreen::shortEjectPress()
{
    const DeviceInfo *dev = _data->GetDevice(kIdeDeviceIndex);
    if (!dev || !dev->present || dev->image[0] == '\0')
        return;  // nothing loaded to eject
    // Pressing the eject button again (while this confirmation is up)
    // confirms the eject rather than closing -- ejectSelectsIndex = kEjectConfirm.
    ShowMenu(kMenuEjectConfirm, "Eject?", kEjectConfirmItems, kEjectConfirmCount, kMenuScale,
             DisplayScreenType::Main, kIdeDeviceIndex, &IDEMainScreen::onMenuAction, this, kEjectConfirm);
}

void IDEMainScreen::shortRotaryPress()
{
    ChangeScreen(DisplayScreenType::Browse, kIdeDeviceIndex);
}

void IDEMainScreen::rotaryChange(int)
{
    // Single device -- nothing to scroll on the main screen.
}

}  // namespace zuluide::display
