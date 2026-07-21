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

// Port of ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/MessageBox.h/.cpp: a
// modal overlay (title + 2 lines) drawn on top of whatever's already on
// screen -- it never clears the framebuffer, matching the original's
// clearScreenOnDraw() returning false, so the screen it interrupted stays
// visible behind the box. Dismissed by any button press (unless
// SetBlocking(true)), or automatically after SetAutoCloseMs() elapses (0 =
// stays until dismissed), returning to whatever screen/index was showing
// beforehand via SetReturnScreen(). Callers normally go through
// screen_registry.h's ShowMessage() helper rather than driving this
// class directly.
//
// Simplified from the original: a plain rectangle outline instead of
// Adafruit_GFX's drawRoundRect (this project's minimal Framebuffer doesn't
// have rounded-rect support, and it's a cosmetic-only difference).

#pragma once

#include "screen.h"

namespace zuluide::display {

class MessageBox : public Screen
{
public:
    explicit MessageBox(Framebuffer128x64 *fb) : Screen(fb) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::MessageBox; }

    void init(int index) override;
    void draw() override;
    void tick() override;

    void shortRotaryPress() override;
    void shortUserPress() override;
    void shortEjectPress() override;
    void rotaryChange(int direction) override;

    void SetText(const char *title, const char *line1, const char *line2);
    void SetReturnScreen(DisplayScreenType type, int index);
    void SetAutoCloseMs(uint32_t ms);
    void SetBlocking(bool blocking);

private:
    char _title[24] = "";
    char _line1[24] = "";
    char _line2[24] = "";

    DisplayScreenType _returnScreen = DisplayScreenType::Main;
    int _returnIndex = -1;
    bool _blocking = false;
    uint32_t _autoCloseMs = 0;
    uint32_t _shownAtMs = 0;

    void dismiss();
};

}  // namespace zuluide::display
