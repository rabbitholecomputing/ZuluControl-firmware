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

// Port of ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/ScreenSaver.h/.cpp --
// all 6 concrete styles plus the "random" meta-selection -- against
// Framebuffer128x64/Ssd1306Display instead of Adafruit_SSD1306, and an
// idle-timeout constant instead of a persisted `controlBoardScreenSaverTimeSec`
// setting (this project has no settings store yet).

#pragma once

#include "framebuffer.h"
#include "ssd1306.h"
#include "vec2d.h"
#include <cstdint>

namespace zuluide::display {

enum class ScreenSaverType
{
    Random,
    Blank,
    RandomPositionLogo,
    BouncingLogo,
    HorizontalScrollingIcons,
    VerticalRainingIcons,
    Lightspeed,
};

constexpr int TOTAL_SCREEN_SAVER_STYLES = 6;  // concrete styles, excludes the Random meta-value

class ScreenSaverController
{
public:
    ScreenSaverController(Framebuffer128x64 *fb, Ssd1306Display *display) : _fb(fb), _display(display) {}

    // Default is SCREENSAVER_RANDOM (task requirement: "make the default
    // Screen Saver the random selection") -- picks a new random concrete
    // style each time it activates, same as ScreenSaver.cpp:342-352.
    void SetConfiguredStyle(ScreenSaverType style) { _configuredStyle = style; }
    void SetIdleTimeoutMs(uint32_t ms) { _idleTimeoutMs = ms; }

    // Call once per display_task tick whenever a button/rotary event
    // occurred this tick (before Tick()). Restarts the idle timer, or -- if
    // the screen saver is currently active -- causes the *next* Tick() to
    // exit it (the same "first press only wakes the display" behavior as
    // control.cpp:1341-1386; display_task is responsible for not also
    // forwarding that same event to the active screen while active).
    void NotifyUserInput() { _userInputDetected = true; }

    bool IsActive() const { return _active; }

    // Advances idle-timer bookkeeping and, while active, the current
    // style's animation. Returns true if it drew this tick (the
    // framebuffer changed and should be pushed to hardware).
    bool Tick();

private:
    Framebuffer128x64 *_fb;
    Ssd1306Display *_display;

    ScreenSaverType _configuredStyle = ScreenSaverType::Random;
    ScreenSaverType _currentSelection = ScreenSaverType::Blank;

    uint32_t _idleTimeoutMs = 120000;  // 2 minutes; no persisted-settings store to read this from yet
    bool _active = false;
    bool _userInputDetected = false;
    bool _timerStarted = false;
    uint32_t _timerStartMs = 0;
    uint32_t _nextChangeMs = 0;

    // DVD-logo styles (ScreenSaver.cpp's scrX/scrY/dirX/dirY).
    int _scrX = 5, _scrY = 5;
    bool _dirX = true, _dirY = true;

    // Scrolling-icons / lightspeed particle pools (ScreenSaver.cpp's sT/size/sA/vectors/points).
    static constexpr int MAX_OBJECT_MANY = 32;
    static constexpr int MAX_OBJECT_FEW = 8;
    uint8_t _iconIdx[MAX_OBJECT_MANY] = {0};
    uint8_t _particleSize[MAX_OBJECT_MANY] = {0};
    bool _particleActive[MAX_OBJECT_MANY] = {false};
    Vec2D _vectors[MAX_OBJECT_MANY];
    Vec2D _points[MAX_OBJECT_MANY];

    void enter();
    void exit();
    bool draw();
    static const uint8_t *iconByIndex(int index);
};

}  // namespace zuluide::display
