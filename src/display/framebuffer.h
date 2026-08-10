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

// Minimal Adafruit_GFX-equivalent primitives, written from scratch against
// the SSD1306's native page-addressed GDDRAM layout (buffer[x + (y/8)*WIDTH],
// bit (y & 7), LSB = top pixel -- matches Adafruit_SSD1306::drawPixel() and
// its display() page/column write sequence, so PushFramebuffer() in
// ssd1306.cpp can stream this array unmodified). Needed because
// Adafruit_GFX/Adafruit_SSD1306 depend on the Arduino Wire/Print APIs that
// don't exist in this bare pico-sdk project.

#pragma once

#include <cstdint>
#include <cstddef>

namespace zuluide::display {

class Framebuffer128x64
{
public:
    static constexpr int WIDTH = 128;
    static constexpr int HEIGHT = 64;
    static constexpr int PAGES = HEIGHT / 8;
    static constexpr size_t SIZE_BYTES = WIDTH * PAGES;

    uint8_t buffer[SIZE_BYTES] = {0};

    void clear();

    // Restricts setPixel() (and everything built on it: drawText/drawBitmap/
    // etc.) to the given rectangle, in addition to the screen bounds --
    // needed to keep a scrolling marquee (InfoScreen's filename) from
    // bleeding into the label text to its left as it slides offscreen.
    // clearClip() restores the default (the whole screen).
    void setClip(int x, int y, int w, int h);
    void clearClip();

    void setPixel(int x, int y, bool on = true);
    void fillRect(int x, int y, int w, int h, bool on = true);
    void drawHLine(int x, int y, int w, bool on = true);
    void drawVLine(int x, int y, int h, bool on = true);
    void drawRectOutline(int x, int y, int w, int h, bool on = true);

    // bitmap: row-major, MSB-first per row, rows padded to a whole byte
    // (Adafruit_GFX::drawBitmap's format) -- matches icons.h's byte arrays.
    void drawBitmap(int x, int y, const uint8_t *bitmap, int w, int h, bool on = true);

    // font5x7.h format: 5 columns per glyph, bit 0 (LSB) = top pixel.
    // `scale` blows each source pixel up into a scale x scale block, same
    // as Adafruit_GFX's setTextSize() -- e.g. the original UI's large SCSI
    // ID digits (InfoScreen.cpp/BrowseScreenFlat.cpp's setTextSize(2)).
    void drawChar(int x, int y, char c, bool on = true, int scale = 1);
    void drawText(int x, int y, const char *text, bool on = true, int scale = 1);

    // 6px advance per glyph (5px glyph + 1px spacing) times scale, matches
    // classic Adafruit_GFX font metrics.
    static int textWidth(const char *text, int scale = 1);

private:
    int _clipX0 = 0, _clipY0 = 0, _clipX1 = WIDTH, _clipY1 = HEIGHT;  // exclusive on X1/Y1
};

}  // namespace zuluide::display
