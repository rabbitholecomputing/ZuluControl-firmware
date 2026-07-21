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

// Partial port of ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/InfoScreen.h/.cpp:
// header with a large (2x) SCSI ID right-aligned and the device-type icon
// immediately to its left (InfoScreen.cpp:50-60, same layout BrowseScreen
// uses), then the loaded image filename as a full-width scrolling marquee
// with no "File:" label (BrowseScreen's unlabeled filename line, rather
// than the original's indented, labeled scroller -- a self-contained
// marquee here rather than the original's ScrollingText widget, matching
// its exact timing: 3px per 360ms step with a 1000ms pause at each end
// before reversing, see scrolling_text.cpp's
// SCROLL_INTERVAL_MS/SCROLL_START_DELAY_MS and Screen.cpp's 360ms
// _nextRefresh gate -- SCROLL_INTERVAL_MS itself is actually unused dead
// code upstream; 360ms is the real cadence), and a labeled folder line
// below it. No size line -- deliberately not fetched, see below.
//
// Folder comes from the same lightweight per-device filenames cache
// Browse already uses (I2C_CLIENT_FETCH_FILENAMES via webui_data.h's
// RequestFilenames()/GetFilenamesJson(), looked up by
// DisplayData::FindFolderForImage(), which finds the cached path whose
// basename matches the loaded image and takes everything before the last
// '/') rather than the heavier per-file I2C_CLIENT_FETCH_IMAGES_JSON,
// which also carries size/isDir this screen doesn't use.
//
// Hidden placeholders (not drawn -- no data source for these yet):
// IsRom/IsRaw overlay icons, BinCue-vs-File label switch, the "Style"
// (BrowseMethod) line, and paging to InfoPage2/3/4 (rotaryChange is a
// no-op here, unlike the original).

#pragma once

#include "screen.h"
#include "display_data.h"
#include "../ZuluControl_config.h"

namespace zuluide::display {

class InfoScreen : public Screen
{
public:
    InfoScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Info; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortUserPress() override;

private:
    DisplayData *_data;
    int _deviceIndex = -1;
    int _scsiId = 0;
    bool _ready = false;
    bool _folderFound = false;
    char _folderText[MAX_FILE_PATH] = "";

    // Ping-pong marquee state, timed to match scrolling_text.cpp/Screen.cpp
    // exactly (see header comment above).
    char _lastImageName[MAX_FILE_PATH] = "";
    int _scrollOffsetPx = 0;
    bool _scrolling = false;
    bool _scrollReverse = false;
    uint32_t _scrollPauseUntilMs = 0;
    uint32_t _lastScrollStepMs = 0;

    void updateScroll(const char *filename);
};

}  // namespace zuluide::display
