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

#include "ui_settings.h"

namespace zuluide::display {

namespace {

// The three scroll steps the Browse menu / Settings screen cycle through.
constexpr int kScrollSteps[] = {1, 10, 50};
constexpr int kScrollStepCount = (int)(sizeof(kScrollSteps) / sizeof(kScrollSteps[0]));

int g_scrollStepIndex = 0;  // -> kScrollSteps[0] == 1

ScreenSaverType g_screenSaverStyle = ScreenSaverType::Random;
bool g_screenSaverNowRequested = false;

}  // namespace

int GetScrollStep()
{
    return kScrollSteps[g_scrollStepIndex];
}

void SetScrollStep(int step)
{
    for (int i = 0; i < kScrollStepCount; i++)
    {
        if (kScrollSteps[i] == step)
        {
            g_scrollStepIndex = i;
            return;
        }
    }
    // Unknown value -- clamp to the smallest step rather than storing it
    // (only the three predefined steps are ever valid here).
    g_scrollStepIndex = 0;
}

void CycleScrollStep()
{
    g_scrollStepIndex = (g_scrollStepIndex + 1) % kScrollStepCount;
}

ScreenSaverType GetConfiguredScreenSaver()
{
    return g_screenSaverStyle;
}

void SetConfiguredScreenSaver(ScreenSaverType style)
{
    g_screenSaverStyle = style;
}

void CycleScreenSaver()
{
    // Cycles Random (0) through the 6 concrete styles and back -- the enum's
    // Random..Lightspeed are contiguous values 0..TOTAL_SCREEN_SAVER_STYLES.
    int next = ((int)g_screenSaverStyle + 1) % (TOTAL_SCREEN_SAVER_STYLES + 1);
    g_screenSaverStyle = (ScreenSaverType)next;
}

const char *ScreenSaverName(ScreenSaverType style)
{
    switch (style)
    {
        case ScreenSaverType::Random: return "Random";
        case ScreenSaverType::Blank: return "Blank";
        case ScreenSaverType::RandomPositionLogo: return "Logo";
        case ScreenSaverType::BouncingLogo: return "Bounce";
        case ScreenSaverType::HorizontalScrollingIcons: return "Scroll";
        case ScreenSaverType::VerticalRainingIcons: return "Rain";
        case ScreenSaverType::Lightspeed: return "Warp";
    }
    return "?";
}

void RequestScreenSaverNow()
{
    g_screenSaverNowRequested = true;
}

bool ConsumeScreenSaverNowRequest()
{
    if (!g_screenSaverNowRequested)
        return false;
    g_screenSaverNowRequested = false;
    return true;
}

}  // namespace zuluide::display
