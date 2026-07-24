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

// Port of ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/Screen.h/.cpp base
// class -- same init()/tick()/draw() lifecycle and input virtuals -- but
// operating on a shared Framebuffer128x64 instead of an Adafruit_SSD1306*
// (no Arduino dependency here), and without the ScrollingText marquee
// subsystem: long strings are truncated/ellipsized to fit instead of
// animating, which is a reasonable simplification for this iteration
// (the original's scrolling_text.h widget is a polish feature, not one of
// the screens/behaviors this iteration was asked to port).

#pragma once

#include "screen_type.h"
#include "framebuffer.h"
#include <cstdint>
#include <cstddef>

namespace zuluide::display {

// Bordered overlay box (same visual language as MessageBox, but wide enough
// for a full-width message), drawn directly onto `fb` with `message` on the
// first line and "<bytesTransferred>/<totalBytes>" (short "12B"/"4kB" form)
// centered below it. A free function (not a Screen method) so
// display_task.cpp's global loading overlay -- which needs to draw over
// whatever screen is active, without being one itself -- can call it
// directly on the shared framebuffer, in addition to Screen subclasses
// calling it on their own _fb.
void DrawLoadingPopup(Framebuffer128x64 *fb, const char *message, size_t bytesTransferred, size_t totalBytes);

class Screen
{
public:
    explicit Screen(Framebuffer128x64 *fb) : _fb(fb) {}
    virtual ~Screen() = default;

    // Called when switched to this screen.
    virtual void init(int index)
    {
        _initIndex = index;
        _hasDrawn = false;
    }

    // Called on every display_task tick; redraws only if forceDraw() was
    // called since the last draw (matches Screen::tick()'s dirty-check,
    // minus the scroller-driven periodic redraw the original also has).
    virtual void tick()
    {
        if (!_hasDrawn)
        {
            _fb->clear();
            draw();
            _hasDrawn = true;
        }
    }

    // Draws the screen's content into _fb. Called by tick(); _fb is
    // already cleared beforehand.
    virtual void draw() {}

    void forceDraw() { _hasDrawn = false; }

    virtual void shortRotaryPress() {}
    virtual void shortUserPress() {}
    virtual void shortEjectPress() {}
    virtual void longRotaryPress() {}
    virtual void longUserPress() {}
    virtual void longEjectPress() {}
    virtual void rotaryChange(int direction) {}

    virtual DisplayScreenType screenType() const = 0;

    int getOriginalIndex() const { return _initIndex; }

protected:
    Framebuffer128x64 *_fb;
    bool _hasDrawn = false;
    int _initIndex = -100;

    // Drawing helpers, ported in spirit from Screen.cpp's
    // printCenteredText()/printNumberFromTheRight()/drawIconFromRight()/
    // makeImageSizeStr(), against Framebuffer128x64's primitives.
    void printCenteredText(const char *text, int y);
    void printRightAligned(const char *text, int rightEdge, int y);
    void drawIconFromRight(const uint8_t *icon, int iconWidth, int iconHeight, int extraSpace, int y);

    // Truncates `text` in-place (writing into `out`, up to outSize-1 chars)
    // to fit within `maxWidthPx`, appending ".." if it had to cut anything --
    // this iteration's stand-in for the original's scrolling marquee text.
    static void ellipsizeToWidth(const char *text, char *out, size_t outSize, int maxWidthPx);

    static void makeImageSizeStr(uint64_t size, char *buffer, size_t bufSize);
};

}  // namespace zuluide::display
