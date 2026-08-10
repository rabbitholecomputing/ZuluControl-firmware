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

#include "framebuffer.h"
#include "font5x7.h"
#include <cstring>

namespace zuluide::display {

void Framebuffer128x64::clear()
{
    memset(buffer, 0, sizeof(buffer));
}

void Framebuffer128x64::setClip(int x, int y, int w, int h)
{
    _clipX0 = x;
    _clipY0 = y;
    _clipX1 = x + w;
    _clipY1 = y + h;
}

void Framebuffer128x64::clearClip()
{
    _clipX0 = 0;
    _clipY0 = 0;
    _clipX1 = WIDTH;
    _clipY1 = HEIGHT;
}

void Framebuffer128x64::setPixel(int x, int y, bool on)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return;
    if (x < _clipX0 || x >= _clipX1 || y < _clipY0 || y >= _clipY1)
        return;

    size_t idx = x + (y / 8) * WIDTH;
    uint8_t mask = 1 << (y & 7);
    if (on)
        buffer[idx] |= mask;
    else
        buffer[idx] &= ~mask;
}

void Framebuffer128x64::fillRect(int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++)
    {
        for (int i = 0; i < w; i++)
        {
            setPixel(x + i, y + j, on);
        }
    }
}

void Framebuffer128x64::drawHLine(int x, int y, int w, bool on)
{
    for (int i = 0; i < w; i++)
        setPixel(x + i, y, on);
}

void Framebuffer128x64::drawVLine(int x, int y, int h, bool on)
{
    for (int j = 0; j < h; j++)
        setPixel(x, y + j, on);
}

void Framebuffer128x64::drawRectOutline(int x, int y, int w, int h, bool on)
{
    drawHLine(x, y, w, on);
    drawHLine(x, y + h - 1, w, on);
    drawVLine(x, y, h, on);
    drawVLine(x + w - 1, y, h, on);
}

void Framebuffer128x64::drawBitmap(int x, int y, const uint8_t *bitmap, int w, int h, bool on)
{
    int byteWidth = (w + 7) / 8;
    for (int j = 0; j < h; j++)
    {
        for (int i = 0; i < w; i++)
        {
            uint8_t b = bitmap[j * byteWidth + i / 8];
            if (b & (0x80 >> (i & 7)))
                setPixel(x + i, y + j, on);
        }
    }
}

void Framebuffer128x64::drawChar(int x, int y, char c, bool on, int scale)
{
    if (scale < 1) scale = 1;
    uint8_t code = static_cast<uint8_t>(c);
    for (int col = 0; col < FONT5X7_GLYPH_WIDTH; col++)
    {
        uint8_t line = font5x7[code * FONT5X7_GLYPH_WIDTH + col];
        for (int row = 0; row < 8; row++, line >>= 1)
        {
            if (line & 1)
            {
                if (scale == 1)
                    setPixel(x + col, y + row, on);
                else
                    fillRect(x + col * scale, y + row * scale, scale, scale, on);
            }
        }
    }
}

void Framebuffer128x64::drawText(int x, int y, const char *text, bool on, int scale)
{
    if (scale < 1) scale = 1;
    int cursor = x;
    for (const char *p = text; *p != '\0'; p++)
    {
        drawChar(cursor, y, *p, on, scale);
        cursor += (FONT5X7_GLYPH_WIDTH + 1) * scale;
    }
}

int Framebuffer128x64::textWidth(const char *text, int scale)
{
    if (scale < 1) scale = 1;
    int len = 0;
    for (const char *p = text; *p != '\0'; p++)
        len++;
    return len > 0 ? (len * (FONT5X7_GLYPH_WIDTH + 1) - 1) * scale : 0;
}

}  // namespace zuluide::display
