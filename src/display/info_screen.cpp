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

#include "info_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include "../webui_data.h"
#include <pico/time.h>
#include <cstdio>
#include <cstring>

namespace zuluide::display {

namespace {
// Marquee geometry: full width, no "File:" label -- matches BrowseScreen's
// unlabeled filename line instead of the original InfoScreen's labeled,
// indented scroller (setupScroller(0, 52, 16, 76, 8, 1), InfoScreen.cpp:36).
constexpr int kScrollX = 0;
constexpr int kScrollY = 16;
constexpr int kScrollW = Framebuffer128x64::WIDTH - kScrollX;

// Matches scrolling_text.cpp's actual behavior exactly: 3px per step, a
// step every 360ms (Screen.cpp's _nextRefresh gate -- the real cadence;
// scrolling_text.cpp's own SCROLL_INTERVAL_MS=60 is defined but never
// referenced anywhere, dead code upstream), 1000ms paused at each end.
constexpr int kScrollStepPx = 3;
constexpr uint32_t kScrollIntervalMs = 360;
constexpr uint32_t kScrollPauseMs = 1000;

// Header: matches InfoScreen.cpp:50-60's setTextSize(2) +
// printNumberFromTheRight(_scsiId, 6, 0) for the large SCSI ID, then
// drawIconFromRight(deviceIcon, 6, 0) for the device-type icon immediately
// to its left -- both use Screen.cpp's chained `_iconX -= 14; _iconX -=
// extraSpace(6)` step (14 = 12px icon width + 2px gap), so a single
// kHeaderStep=20 reproduces the same spacing (see also BrowseScreen, which
// uses the identical layout).
constexpr int kHeaderStep = 20;
constexpr int kHeaderIconSize = 12;

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }
}  // namespace

void InfoScreen::init(int index)
{
    Screen::init(index);

    _deviceIndex = index;
    const DeviceInfo *dev = _data->GetDevice(index);
    _scsiId = dev ? dev->id : 0;
    _ready = false;
    _folderFound = false;
    _folderText[0] = '\0';
    _lastImageName[0] = '\0';
    _scrollOffsetPx = 0;
    _scrolling = false;
    _scrollReverse = false;

    forceDraw();
}

void InfoScreen::updateScroll(const char *filename)
{
    uint32_t now = millis();

    // Text changed (including first call after init()) -- reset and pause
    // before scrolling starts, same as ScrollingText::SetToDisplay()->Reset().
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

void InfoScreen::tick()
{
    _data->Refresh();
    _ready = RequestFilenames(_scsiId);

    const DeviceInfo *dev = _data->GetDevice(_deviceIndex);
    if (_ready && dev && dev->image[0] != '\0')
        _folderFound = _data->FindFolderForImage(_scsiId, dev->image, _folderText, sizeof(_folderText));
    else
        _folderFound = false;

    if (dev)
        updateScroll(dev->image[0] != '\0' ? dev->image : "");

    forceDraw();
    Screen::tick();
}

void InfoScreen::draw()
{
    const DeviceInfo *dev = _data->GetDevice(_deviceIndex);

    _fb->drawText(0, 0, "Info");

    // Header: large (2x) SCSI ID right-aligned, device-type icon
    // immediately to its left, divider line ending just short of the
    // cluster -- see InfoScreen.cpp:50-60.
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

    // Hidden placeholders: IsRom/IsRaw overlay icons, BinCue-vs-File label
    // switch, and the "Style" (BrowseMethod) line -- no data source for
    // these yet.

    if (!dev || !dev->present)
    {
        printCenteredText("No device", 28);
        return;
    }

    if (dev->image[0] == '\0')
    {
        _fb->drawText(kScrollX, 16, "(ejected)");
    }
    else
    {
        _fb->setClip(kScrollX, kScrollY, kScrollW, 8);
        _fb->drawText(kScrollX - _scrollOffsetPx, kScrollY, dev->image);
        _fb->clearClip();
    }

    // Own indent based on the label's actual width -- must not reuse
    // kScrollX (0, now that the filename line above is full-width/unlabeled),
    // or the value would draw on top of the label.
    static const char kFolderLabel[] = "Folder: ";
    int folderLabelW = Framebuffer128x64::textWidth(kFolderLabel);
    _fb->drawText(0, 28, kFolderLabel);

    if (!_ready)
    {
        _fb->drawText(folderLabelW, 28, "...");
    }
    else if (_folderFound)
    {
        char folderBuf[26];
        ellipsizeToWidth(_folderText, folderBuf, sizeof(folderBuf), Framebuffer128x64::WIDTH - folderLabelW);
        _fb->drawText(folderLabelW, 28, folderBuf);
    }
}

void InfoScreen::shortUserPress()
{
    ChangeScreen(DisplayScreenType::Main, _deviceIndex);
}

}  // namespace zuluide::display
