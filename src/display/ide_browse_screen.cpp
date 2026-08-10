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

#include "ide_browse_screen.h"
#include "screen_registry.h"
#include "menu_screen.h"
#include "ui_settings.h"
#include "../ZuluControlI2CClient.h"
#include "../webui_data.h"
#include <pico/time.h>
#include <cstdio>
#include <cstring>
#include <memory>

namespace zuluide::display {

namespace {
// Single implicit IDE device, always slot 0.
constexpr int kIdeDeviceIndex = 0;

// Marquee geometry/timing -- identical to InfoScreen's filename scroller.
constexpr int kScrollX = 0;
constexpr int kScrollY = 16;
constexpr int kScrollW = Framebuffer128x64::WIDTH - kScrollX;
constexpr int kScrollStepPx = 3;
constexpr uint32_t kScrollIntervalMs = 360;
constexpr uint32_t kScrollPauseMs = 1000;

// Browse-menu actions (stored per-item in _menuActions, since the item list
// is built dynamically in buildMenu()).
enum { kActSelect = 0, kActClose, kActMain, kActScroll1, kActScroll10, kActScroll50 };

// Single tag for the browse menu passed to ShowMenu().
constexpr int kMenuBrowse = 0;
constexpr int kMenuScale = 1;

// Virtual list entry shown at the ring's wrap point (cursor index == image
// count): selecting it returns to the main screen instead of loading an
// image. So the navigable range is [0, total], where `total` is this entry.
// Drawn as two centered lines (see draw()); kBackToMainLabel is the combined
// form updateScroll() uses only to detect the entry changing.
constexpr const char *kBackToMainLabel = "Back to Main Screen";
constexpr const char *kBackToMainLine1 = "Back to";
constexpr const char *kBackToMainLine2 = "Main Screen";

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }
}  // namespace

void IDEBrowseScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();

    // Keep the browsed position across a menu round-trip (scroll change /
    // close / eject scroll-reset); otherwise start at the first image.
    if (_preserveCursor)
        _preserveCursor = false;
    else
        _cursor = 0;
    _ready = false;

    _lastScrollName[0] = '\0';
    _scrollOffsetPx = 0;
    _scrolling = false;
    _scrollReverse = false;

    forceDraw();
}

void IDEBrowseScreen::updateScroll(const char *filename)
{
    uint32_t now = millis();

    if (strcmp(filename, _lastScrollName) != 0)
    {
        snprintf(_lastScrollName, sizeof(_lastScrollName), "%s", filename);
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

void IDEBrowseScreen::tick()
{
    _data->Refresh();
    _ready = RequestFilenames(kIdeDeviceIndex);

    if (_ready)
    {
        int total = _data->IndexedFilenameCount(kIdeDeviceIndex);
        if (total > 0)
        {
            // Cursor ranges over [0, total]; index == total is the virtual
            // "Back to Main Screen" entry at the ring's wrap point.
            if (_cursor > total) _cursor = total;
            if (_cursor < 0) _cursor = 0;

            if (_cursor == total)
            {
                updateScroll(kBackToMainLabel);
            }
            else
            {
                auto filenameBuf = std::make_unique<char[]>(MAX_FILE_PATH);
                if (_data->GetIndexedFilename(kIdeDeviceIndex, _cursor, nullptr, 0, filenameBuf.get(), MAX_FILE_PATH))
                    updateScroll(filenameBuf.get());
            }
        }
    }

    forceDraw();
    Screen::tick();
}

void IDEBrowseScreen::draw()
{
    _fb->drawText(0, 0, "Browse");

    int step = GetScrollStep();
    if (step > 1)
    {
        // Show the active scroll multiplier so a >1 step (which also changes
        // what the eject button does) is visible, right-aligned in the header.
        char stepText[12];
        snprintf(stepText, sizeof(stepText), "x%d", step);
        printRightAligned(stepText, Framebuffer128x64::WIDTH, 0);
    }

    _fb->drawHLine(0, 10, Framebuffer128x64::WIDTH);

    // No popup drawn here -- display_task.cpp's global loading overlay
    // covers any screen while a filenames fetch is in flight (see
    // IsFilenamesFetchActive()), since the server can also push a filenames
    // update unprompted with no screen having called RequestFilenames().
    if (!_ready)
        return;

    int total = _data->IndexedFilenameCount(kIdeDeviceIndex);
    if (total == 0)
    {
        printCenteredText("No images", 28);
        return;
    }

    // _cursor is already clamped to [0, total] by tick().
    if (_cursor == total)
    {
        // Virtual "Back to Main Screen" entry -- two centered lines, no
        // "n of total" counter.
        printCenteredText(kBackToMainLine1, 20);
        printCenteredText(kBackToMainLine2, 32);
        return;
    }

    // Only the filename is needed now (the Folder line was removed).
    auto filenameBuf = std::make_unique<char[]>(MAX_FILE_PATH);
    if (!_data->GetIndexedFilename(kIdeDeviceIndex, _cursor, nullptr, 0, filenameBuf.get(), MAX_FILE_PATH))
        return;

    // Scrolling marquee -- advanced once per tick() in updateScroll().
    _fb->setClip(kScrollX, kScrollY, kScrollW, 8);
    _fb->drawText(kScrollX - _scrollOffsetPx, kScrollY, filenameBuf.get());
    _fb->clearClip();

    char counter[24];
    snprintf(counter, sizeof(counter), "%d of %d", _cursor + 1, total);
    printRightAligned(counter, Framebuffer128x64::WIDTH, 40);
}

void IDEBrowseScreen::loadSelected()
{
    // The virtual wrap-point entry (cursor == image count) isn't an image --
    // selecting it returns to the main screen. Reached from both the rotary
    // push and the menu's "Select".
    int total = _data->IndexedFilenameCount(kIdeDeviceIndex);
    if (total > 0 && _cursor == total)
    {
        ChangeScreen(DisplayScreenType::Main, kIdeDeviceIndex);
        return;
    }

    auto pathBuf = std::make_unique<char[]>(MAX_FILE_PATH);
    if (!_data->GetIndexedFilename(kIdeDeviceIndex, _cursor, pathBuf.get(), MAX_FILE_PATH, nullptr, 0))
        return;

    // ZuluIDE takes a path-only LOAD_IMAGE payload (no scsi_id prefix).
    zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_LOAD_IMAGE, pathBuf.get());
    ShowMessage("-- Info --", "Loading", "Image...", DisplayScreenType::Main, kIdeDeviceIndex, 1200);
}

void IDEBrowseScreen::buildMenu()
{
    // Always Select + Close, then the scroll steps -- but a step larger than
    // the number of images in the list is pointless, so hide any Scroll N
    // whose N exceeds the item count (Scroll 1 is always offered). Task spec:
    // "If less items exist than the scroll count, hide that scroll number."
    int total = _data->IndexedFilenameCount(kIdeDeviceIndex);
    int n = 0;

    _menuItems[n] = "Select";      _menuActions[n] = kActSelect;  n++;
    _menuItems[n] = "Close";       _menuActions[n] = kActClose;   n++;
    _menuItems[n] = "Main Screen"; _menuActions[n] = kActMain;    n++;
    _menuItems[n] = "Scroll 1";    _menuActions[n] = kActScroll1; n++;
    if (total >= 10) { _menuItems[n] = "Scroll 10"; _menuActions[n] = kActScroll10; n++; }
    if (total >= 50) { _menuItems[n] = "Scroll 50"; _menuActions[n] = kActScroll50; n++; }

    _menuCount = n;
}

void IDEBrowseScreen::onMenuAction(void *ctx, int, int selected)
{
    auto *self = static_cast<IDEBrowseScreen *>(ctx);
    int action = (selected >= 0 && selected < self->_menuCount) ? self->_menuActions[selected] : kActClose;

    switch (action)
    {
        // Select and Main Screen navigate away; the scroll/close actions
        // return to Browse -- keep the current image (don't reset to the top).
        case kActSelect:   self->loadSelected(); return;
        case kActMain:     ChangeScreen(DisplayScreenType::Main, kIdeDeviceIndex); return;
        case kActScroll1:  SetScrollStep(1);  self->_preserveCursor = true; return;
        case kActScroll10: SetScrollStep(10); self->_preserveCursor = true; return;
        case kActScroll50: SetScrollStep(50); self->_preserveCursor = true; return;
        case kActClose:
        default:           self->_preserveCursor = true; return;
    }
}

void IDEBrowseScreen::shortUserPress()
{
    buildMenu();
    ShowMenu(kMenuBrowse, "Browse Menu", _menuItems, _menuCount, kMenuScale,
             DisplayScreenType::Browse, kIdeDeviceIndex, &IDEBrowseScreen::onMenuAction, this);
}

void IDEBrowseScreen::shortRotaryPress()
{
    loadSelected();
}

void IDEBrowseScreen::shortEjectPress()
{
    // Eject leaves for Main -- but when a >1 scroll step is active, the task
    // wants eject to first reset the step to 1 and show a brief notice rather
    // than navigating away (a moved rotary dial then dismisses that notice,
    // since MessageBox closes on any input).
    if (GetScrollStep() > 1)
    {
        SetScrollStep(1);
        _preserveCursor = true;  // returns to Browse -- keep the current image
        ShowMessage("-- Info --", "Scroll reset", "to 1", DisplayScreenType::Browse, kIdeDeviceIndex, 1000);
        return;
    }

    ChangeScreen(DisplayScreenType::Main, kIdeDeviceIndex);
}

void IDEBrowseScreen::rotaryChange(int direction)
{
    int total = _data->IndexedFilenameCount(kIdeDeviceIndex);
    if (total <= 0)
        return;

    // Ring covers [0, total]: the extra slot (index == total) is the virtual
    // "Back to Main Screen" entry. A large scroll step (10/50) must not jump
    // over that slot when it wraps, so any move that would cross the end
    // (forward) or the start (backward) from a real image lands ON the
    // sentinel instead. From the sentinel itself, a step moves normally into
    // the list (so it isn't a trap you can't leave with a big step).
    int navCount = total + 1;
    int step = GetScrollStep();

    if (direction > 0)
    {
        if (_cursor != total && _cursor + step >= total)
            _cursor = total;  // crossing the end -> stop on the sentinel
        else
            _cursor = (_cursor + step) % navCount;
    }
    else if (direction < 0)
    {
        if (_cursor != total && _cursor - step < 0)
            _cursor = total;  // crossing the start -> stop on the sentinel
        else
            _cursor = ((_cursor - step) % navCount + navCount) % navCount;
    }
    forceDraw();
}

}  // namespace zuluide::display
