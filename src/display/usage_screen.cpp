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

#include "usage_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include "../ZuluControlI2CClient.h"  // FILENAMES_JSON_CACHE_SIZE
#include "../webui_data.h"
#include <cstdio>

namespace zuluide::display {

namespace {

// Label/value row baselines under the title rule at y=10, with the value line
// indented under its label. The action hint sits on the same baseline the WiFi
// page uses for its action bar (wifi_screen.cpp's kBarY).
constexpr int kCacheLabelY = 14;
constexpr int kCacheValueY = 24;
constexpr int kIndexLabelY = 36;
constexpr int kIndexValueY = 46;
constexpr int kHintY = 54;
constexpr int kValueX = 8;

// Whole bytes below 1kB, one decimal above -- the same shape as
// Screen::makeImageSizeStr's "4.2k" form, but written here so the unit suffix
// ends in "B" ("4.2kB" / "512B") rather than that function's bare "4.2k".
// Sizes here never exceed FILENAMES_JSON_CACHE_SIZE (50kB), so kB is the
// largest unit needed.
void formatBytes(size_t bytes, char *out, size_t outSize)
{
    if (bytes < 1024)
        snprintf(out, outSize, "%uB", (unsigned)bytes);
    else
        snprintf(out, outSize, "%u.%ukB", (unsigned)(bytes / 1024),
                 (unsigned)(((bytes % 1024) * 10) / 1024));
}

}  // namespace

void UsageScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();
    forceDraw();
}

void UsageScreen::tick()
{
    // Same cadence as the Settings screen it is reached from: Refresh() is
    // internally throttled, so a redraw per tick keeps both figures live while
    // a filenames fetch is filling the cache.
    _data->Refresh();
    forceDraw();
    Screen::tick();
}

void UsageScreen::draw()
{
    _fb->drawText(0, 0, "Usage");
    _fb->drawHLine(0, 10, 112);

    if (_data->SdPresent())
        _fb->drawBitmap(115, 0, icon_sd, 12, 12);
    else
        _fb->drawBitmap(115, 0, icon_nosd, 12, 12);

    char used[12], total[12], line[48];

    formatBytes(GetFilenamesCacheBytesUsed(), used, sizeof(used));
    formatBytes(FILENAMES_JSON_CACHE_SIZE, total, sizeof(total));
    snprintf(line, sizeof(line), "%s of %s", used, total);
    _fb->drawText(0, kCacheLabelY, "Filename cache");
    _fb->drawText(kValueX, kCacheValueY, line);

    snprintf(line, sizeof(line), "%d of %d images",
             _data->IndexedFilenameTotal(), DisplayData::MAX_FILENAME_INDEX);
    _fb->drawText(0, kIndexLabelY, "Cache index");
    _fb->drawText(kValueX, kIndexValueY, line);

    _fb->drawText(0, kHintY, "usr:back");
}

void UsageScreen::back()
{
    ChangeScreen(DisplayScreenType::Settings, getOriginalIndex());
}

void UsageScreen::shortUserPress() { back(); }
void UsageScreen::shortEjectPress() { back(); }
void UsageScreen::shortRotaryPress() { back(); }

}  // namespace zuluide::display
