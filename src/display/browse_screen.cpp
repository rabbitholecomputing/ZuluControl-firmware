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
            if (_cursor >= total) _cursor = total - 1;
            if (_cursor < 0) _cursor = 0;

            // Heap-allocated (not a stack local) -- sized to the full
            // MAX_FILE_PATH, same convention as draw()/shortRotaryPress().
            auto filenameBuf = std::make_unique<char[]>(MAX_FILE_PATH);
            if (_data->GetIndexedFilename(_scsiId, _cursor, nullptr, 0, filenameBuf.get(), MAX_FILE_PATH))
                updateScroll(filenameBuf.get());
        }
    }

    forceDraw();
    Screen::tick();
}

void BrowseScreen::draw()
{
    _fb->drawText(0, 0, "Browse");

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

    if (!_ready)
    {
        printCenteredText("Loading...", 28);
        return;
    }

    int total = _data->IndexedFilenameCount(_scsiId);
    if (total == 0)
    {
        printCenteredText("No images", 28);
        return;
    }

    // _cursor is already clamped to [0, total) by tick(), which runs
    // before every draw() (see Screen::tick()).

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
    _cursor = (_cursor + direction + total) % total;
    forceDraw();
}

void BrowseScreen::shortUserPress()
{
    ChangeScreen(DisplayScreenType::Main, -1);
}

void BrowseScreen::shortEjectPress()
{
    shortRotaryPress();
}

void BrowseScreen::shortRotaryPress()
{
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

    // Matches control.cpp's loadImageDeferred() exactly: "-- Info --" /
    // "Loading" / "Image..." -- no filename (control.cpp:383).
    ShowMessage("-- Info --", "Loading", "Image...", DisplayScreenType::Main, getOriginalIndex(), 1200);
}

}  // namespace zuluide::display
