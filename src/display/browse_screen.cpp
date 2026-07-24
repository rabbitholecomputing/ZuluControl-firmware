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

#include "browse_screen.h"
#include "screen_registry.h"
#include "menu_screen.h"
#include "ui_settings.h"
#include "icons.h"
#include "../ZuluControlI2CClient.h"
#include "../ZuluControl_config.h"
#include "../webui_data.h"
#include <pico/time.h>
#include <cstdio>
#include <cstring>
#include <memory>

namespace zuluide::display {

namespace {
// Matches BrowseScreenFlat.cpp's header layout exactly: setTextSize(2) +
// printNumberFromTheRight(_scsiId, 6, 0) for the large SCSI ID, then
// drawIconFromRight(deviceIcon, 6, 0) for the device-type icon immediately
// to its left -- both use Screen.cpp's chained `_iconX -= 14; _iconX -=
// extraSpace(6)` step (14 = 12px icon width + 2px gap), so a single
// kHeaderStep=20 reproduces the same spacing here.
constexpr int kHeaderStep = 20;
constexpr int kHeaderIconSize = 12;

// Marquee geometry/timing -- identical to InfoScreen's filename scroller
// (info_screen.cpp): full width, no label, 3px per 360ms step, 1000ms
// pause at each end before reversing.
constexpr int kScrollX = 0;
constexpr int kScrollY = 16;
constexpr int kScrollW = Framebuffer128x64::WIDTH - kScrollX;
constexpr int kScrollStepPx = 3;
constexpr uint32_t kScrollIntervalMs = 360;
constexpr uint32_t kScrollPauseMs = 1000;

// Browse-menu actions (stored per-item in _menuActions, since the item list is
// built dynamically in buildMenu()).
enum { kActSelect = 0, kActClose, kActMap, kActInfo, kActScroll1, kActScroll10, kActScroll50 };

// Single tag for the browse menu passed to ShowMenu().
constexpr int kMenuBrowse = 0;
constexpr int kMenuScale = 1;

// Virtual list entry shown at the ring's wrap point (cursor index == image
// count): selecting it returns to the Info screen instead of loading an image
// -- mirrors the ZuluIDE browse screen's "Back to Main Screen" sentinel (which
// goes to Main), but SCSI browse is entered from Info, so it goes back there.
// So the navigable range is [0, total], where `total` is this entry. Drawn as
// two centered lines (see draw()); kBackToInfoLabel is the combined form
// updateScroll() uses only to detect the entry changing.
constexpr const char *kBackToInfoLabel = "Back to Info Screen";
constexpr const char *kBackToInfoLine1 = "Back to";
constexpr const char *kBackToInfoLine2 = "Info Screen";

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }
}  // namespace

void BrowseScreen::updateScroll(const char *filename)
{
    uint32_t now = millis();

    // Text changed (including first call after init()) -- reset and pause
    // before scrolling starts, same as InfoScreen::updateScroll().
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
            return;  // still pausing at this end
        _scrolling = true;
        _lastScrollStepMs = now;
    }

    if ((int32_t)(now - _lastScrollStepMs) < (int32_t)kScrollIntervalMs)
        return;  // not time for the next 3px step yet
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

void BrowseScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();

    const DeviceInfo *dev = _data->GetDevice(index);
    _scsiId = dev ? dev->id : 0;
    // Keep the browsed position across a menu round-trip (scroll change /
    // close); otherwise start at the first image.
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

void BrowseScreen::tick()
{
    _data->Refresh();  // also rebuilds the filename index for every device when new JSON arrives
    _ready = RequestFilenames(_scsiId);

    if (_ready)
    {
        int total = _data->IndexedFilenameCount(_scsiId);
        if (total > 0)
        {
            // Cursor ranges over [0, total]; index == total is the virtual
            // "Back to Info Screen" entry at the ring's wrap point.
            if (_cursor > total) _cursor = total;
            if (_cursor < 0) _cursor = 0;

            if (_cursor == total)
            {
                updateScroll(kBackToInfoLabel);
            }
            else
            {
                // Heap-allocated (not a stack local) -- sized to the full
                // MAX_FILE_PATH, same convention as draw()/shortRotaryPress().
                auto filenameBuf = std::make_unique<char[]>(MAX_FILE_PATH);
                if (_data->GetIndexedFilename(_scsiId, _cursor, nullptr, 0, filenameBuf.get(), MAX_FILE_PATH))
                    updateScroll(filenameBuf.get());
            }
        }
    }

    forceDraw();
    Screen::tick();
}

void BrowseScreen::draw()
{
    _fb->drawText(0, 0, "Browse");

    // Show the active scroll multiplier (just right of the title) when it's
    // above 1, so a >1 step is visible -- the big SCSI ID owns the far right.
    int step = GetScrollStep();
    if (step > 1)
    {
        char stepText[12];
        snprintf(stepText, sizeof(stepText), "x%d", step);
        _fb->drawText(Framebuffer128x64::textWidth("Browse") + 4, 0, stepText);
    }

    // Header: large (2x) SCSI ID right-aligned, device-type icon
    // immediately to its left, divider line ending just short of the
    // cluster -- see BrowseScreenFlat.cpp:184-200.
    const DeviceInfo *dev = _data->GetDevice(_scsiId);

    char idText[12];
    snprintf(idText, sizeof(idText), "%d", _scsiId);
    int numW = Framebuffer128x64::textWidth(idText, 2);
    int numX = Framebuffer128x64::WIDTH - numW;
    _fb->drawText(numX, 0, idText, true, 2);

    int iconX = numX - kHeaderStep;
    if (dev && dev->present)
        _fb->drawBitmap(iconX, 0, IconForDeviceType(dev->type), kHeaderIconSize, kHeaderIconSize);

    int lineEnd = iconX - kHeaderStep + 11;
    if (lineEnd < 0) lineEnd = 0;
    _fb->drawHLine(0, 10, lineEnd);

    // No popup drawn here -- display_task.cpp's global loading overlay
    // covers any screen while a filenames fetch is in flight (see
    // IsFilenamesFetchActive()), since the server can also push a filenames
    // update unprompted with no screen having called RequestFilenames().
    if (!_ready)
        return;

    int total = _data->IndexedFilenameCount(_scsiId);
    if (total == 0)
    {
        printCenteredText("No images", 28);
        return;
    }

    // _cursor is already clamped to [0, total] by tick(), which runs
    // before every draw() (see Screen::tick()).
    if (_cursor == total)
    {
        // Virtual "Back to Info Screen" entry -- two centered lines, no folder
        // line or "n of total" counter.
        printCenteredText(kBackToInfoLine1, 20);
        printCenteredText(kBackToInfoLine2, 32);
        return;
    }

    // Looked up directly from the index built once in tick() -- no
    // re-scan of the array to get from one filename to the next.
    auto pathBuf = std::make_unique<char[]>(MAX_FILE_PATH);
    auto filenameBuf = std::make_unique<char[]>(MAX_FILE_PATH);
    if (!_data->GetIndexedFilename(_scsiId, _cursor, pathBuf.get(), MAX_FILE_PATH, filenameBuf.get(), MAX_FILE_PATH))
        return;

    // Scrolling marquee -- state advanced once per tick() in updateScroll(),
    // identical timing to InfoScreen's filename scroller.
    _fb->setClip(kScrollX, kScrollY, kScrollW, 8);
    _fb->drawText(kScrollX - _scrollOffsetPx, kScrollY, filenameBuf.get());
    _fb->clearClip();

    // "Folder: " + directory only (substring out everything before the
    // last '/', mirroring how GetIndexedFilename() substrings out the
    // basename after it) -- matches InfoScreen's "Folder:" label instead
    // of showing the full path+filename.
    static const char kFolderLabel[] = "Folder: ";
    int labelW = Framebuffer128x64::textWidth(kFolderLabel);
    _fb->drawText(0, 28, kFolderLabel);

    // Heap-allocated (not a stack local) since it's sized to the full
    // MAX_FILE_PATH.
    auto dirText = std::make_unique<char[]>(MAX_FILE_PATH);
    const char *lastSlash = strrchr(pathBuf.get(), '/');
    if (lastSlash && lastSlash != pathBuf.get())
        snprintf(dirText.get(), MAX_FILE_PATH, "%.*s", (int)(lastSlash - pathBuf.get()), pathBuf.get());
    else
        snprintf(dirText.get(), MAX_FILE_PATH, "/");

    char dirBuf[26];
    ellipsizeToWidth(dirText.get(), dirBuf, sizeof(dirBuf), Framebuffer128x64::WIDTH - labelW);
    _fb->drawText(labelW, 28, dirBuf);

    char counter[24];
    snprintf(counter, sizeof(counter), "%d of %d", _cursor + 1, total);
    printRightAligned(counter, Framebuffer128x64::WIDTH, 40);

    // Hidden placeholder: image size / <dir> distinction, and real
    // subfolder navigation -- filenames_json only carries flat paths (see
    // header comment), so size is hidden for now rather than shown as 0.
}

void BrowseScreen::rotaryChange(int direction)
{
    int total = _data->IndexedFilenameCount(_scsiId);
    if (total <= 0)
        return;

    // Ring covers [0, total]: the extra slot (index == total) is the virtual
    // "Back to Info Screen" entry. A large scroll step (10/50) must not jump
    // over that slot when it wraps, so any move that would cross the end
    // (forward) or the start (backward) from a real image lands ON the sentinel
    // instead. From the sentinel itself, a step moves normally into the list
    // (so it isn't a trap you can't leave with a big step). Copied from the
    // ZuluIDE browse screen's rotaryChange().
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

void BrowseScreen::loadSelected()
{
    // The virtual wrap-point entry (cursor == image count) isn't an image --
    // selecting it returns to the Info screen. Reached from both the rotary
    // push and the menu's "Select".
    int total = _data->IndexedFilenameCount(_scsiId);
    if (total > 0 && _cursor == total)
    {
        ChangeScreen(DisplayScreenType::Info, getOriginalIndex());
        return;
    }

    // Only the path is needed for LOAD_IMAGE -- looked up directly from
    // the index, same as draw().
    auto pathBuf = std::make_unique<char[]>(MAX_FILE_PATH);
    if (!_data->GetIndexedFilename(_scsiId, _cursor, pathBuf.get(), MAX_FILE_PATH, nullptr, 0))
        return;

    if (_data->GetDeviceType() == DeviceType::ZuluSCSI)
    {
        // Heap-allocated (not a stack local) -- sized to the full
        // MAX_FILE_PATH plus the leading scsi_id byte.
        auto buf = std::make_unique<uint8_t[]>(1 + MAX_FILE_PATH);
        buf[0] = (uint8_t)_scsiId;
        size_t pathLen = strlen(pathBuf.get());
        memcpy(buf.get() + 1, pathBuf.get(), pathLen);
        zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_LOAD_IMAGE, buf.get(), (uint16_t)(1 + pathLen));
    }
    else
    {
        zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_LOAD_IMAGE, pathBuf.get());
    }

    // Task: selecting an image loads it and returns to the Info screen. The
    // loading MessageBox auto-closes onto Info (control.cpp's loadImageDeferred()
    // text: "-- Info --" / "Loading" / "Image...").
    ShowMessage("-- Info --", "Loading", "Image...", DisplayScreenType::Info, getOriginalIndex(), 1200);
}

void BrowseScreen::buildMenu()
{
    // Like the ZuluIDE browse menu (Select / Close / Scroll N), but with a
    // "Map" entry -- go to the SCSI Map -- added right after "Close" (task
    // spec). A scroll step larger than the number of images is pointless, so
    // hide any Scroll N whose N exceeds the item count (Scroll 1 is always
    // offered), same rule the ZuluIDE browse menu uses.
    int total = _data->IndexedFilenameCount(_scsiId);
    int n = 0;

    _menuItems[n] = "Select";    _menuActions[n] = kActSelect;  n++;
    _menuItems[n] = "Close";     _menuActions[n] = kActClose;   n++;
    _menuItems[n] = "Map";       _menuActions[n] = kActMap;     n++;
    _menuItems[n] = "Info";      _menuActions[n] = kActInfo;    n++;
    _menuItems[n] = "Scroll 1";  _menuActions[n] = kActScroll1; n++;
    if (total >= 10) { _menuItems[n] = "Scroll 10"; _menuActions[n] = kActScroll10; n++; }
    if (total >= 50) { _menuItems[n] = "Scroll 50"; _menuActions[n] = kActScroll50; n++; }

    _menuCount = n;
}

void BrowseScreen::onMenuAction(void *ctx, int, int selected)
{
    auto *self = static_cast<BrowseScreen *>(ctx);
    int action = (selected >= 0 && selected < self->_menuCount) ? self->_menuActions[selected] : kActClose;

    switch (action)
    {
        // Select, Map and Info navigate away; the scroll/close actions return
        // to Browse -- keep the current image (don't reset to the top).
        case kActSelect:   self->loadSelected(); return;
        case kActMap:      ChangeScreen(DisplayScreenType::Main, self->getOriginalIndex()); return;
        case kActInfo:     ChangeScreen(DisplayScreenType::Info, self->getOriginalIndex()); return;
        case kActScroll1:  SetScrollStep(1);  self->_preserveCursor = true; return;
        case kActScroll10: SetScrollStep(10); self->_preserveCursor = true; return;
        case kActScroll50: SetScrollStep(50); self->_preserveCursor = true; return;
        case kActClose:
        default:           self->_preserveCursor = true; return;
    }
}

void BrowseScreen::shortUserPress()
{
    buildMenu();
    ShowMenu(kMenuBrowse, "Browse Menu", _menuItems, _menuCount, kMenuScale,
             DisplayScreenType::Browse, getOriginalIndex(), &BrowseScreen::onMenuAction, this);
}

void BrowseScreen::shortEjectPress()
{
    // Task: if a >1 scroll step is active, the eject button first resets the
    // step to 1 (and shows a brief notice, staying on Browse) rather than
    // leaving; otherwise it returns to the Info screen.
    if (GetScrollStep() > 1)
    {
        SetScrollStep(1);
        _preserveCursor = true;  // returns to Browse -- keep the current image
        ShowMessage("-- Info --", "Scroll reset", "to 1", DisplayScreenType::Browse, getOriginalIndex(), 1000);
        return;
    }

    ChangeScreen(DisplayScreenType::Info, getOriginalIndex());
}

void BrowseScreen::shortRotaryPress()
{
    // Task: the rotary button selects the image and goes to the Info screen.
    loadSelected();
}

}  // namespace zuluide::display
