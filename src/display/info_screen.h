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
// (BrowseMethod) line, and paging to InfoPage2/3/4 (the original paged these
// with the rotary dial; here the dial drives the bottom action bar instead).

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

    // Task-specified button mapping:
    //  - rotary dial    -> move the ▶ marker along the bottom action bar
    //                      (Browse / Eject-or-Insert / Map), wrapping both ways
    //  - rotary push    -> activate the marked action (defaults to Browse)
    //  - user button    -> open the main menu (Browse / SCSI Map / Close /
    //                      Screensaver / Settings), formatted like the ZuluIDE
    //                      main menu
    //  - eject button   -> eject the drive directly
    void shortUserPress() override;
    void shortRotaryPress() override;
    void shortEjectPress() override;
    void rotaryChange(int direction) override;

private:
    DisplayData *_data;
    int _deviceIndex = -1;
    int _scsiId = 0;
    bool _ready = false;
    bool _folderFound = false;
    char _folderText[MAX_FILE_PATH] = "";

    // Bottom action-bar selection: 0 = Browse, 1 = Eject/Insert, 2 = Map. The
    // middle entry shows "Insert" when the drive is ejected and "Eject" when a
    // medium is loaded (task spec). Defaults to Browse (index 0) on every init.
    enum { kBarBrowse = 0, kBarEjectInsert = 1, kBarMap = 2, kBarCount = 3 };
    int _barSel = kBarBrowse;

    void drawActionBar();
    void activateBar();
    void ejectDrive();
    void insertDrive();

    // Eject/(Insert)/Cancel confirmation menu the eject button raises -- same
    // options as the SCSI Map's. "Insert" is only present while ejected, so the
    // per-row action is recorded rather than assumed by position.
    static constexpr int kMaxEjectItems = 3;
    const char *_ejItems[kMaxEjectItems] = {nullptr};
    int _ejActions[kMaxEjectItems] = {0};
    int _ejCount = 0;
    void buildEjectMenu();

    // MenuScreen activation callback (ctx == this) for both the user-button
    // menu and the eject-confirmation menu (distinguished by menuId).
    static void onMenuAction(void *ctx, int menuId, int selected);

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
