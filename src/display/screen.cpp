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

#include "screen.h"
#include <cstring>
#include <cstdio>

namespace zuluide::display {

void Screen::printCenteredText(const char *text, int y)
{
    int w = Framebuffer128x64::textWidth(text);
    int x = (Framebuffer128x64::WIDTH - w) / 2;
    if (x < 0) x = 0;
    _fb->drawText(x, y, text);
}

void Screen::printRightAligned(const char *text, int rightEdge, int y)
{
    int w = Framebuffer128x64::textWidth(text);
    _fb->drawText(rightEdge - w, y, text);
}

void Screen::drawIconFromRight(const uint8_t *icon, int iconWidth, int iconHeight, int extraSpace, int y)
{
    int x = Framebuffer128x64::WIDTH - iconWidth - extraSpace;
    _fb->drawBitmap(x, y, icon, iconWidth, iconHeight);
}

void Screen::ellipsizeToWidth(const char *text, char *out, size_t outSize, int maxWidthPx)
{
    if (outSize == 0)
        return;

    int full = Framebuffer128x64::textWidth(text);
    if (full <= maxWidthPx)
    {
        strncpy(out, text, outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    static const char *ellipsis = "..";
    int ellipsisW = Framebuffer128x64::textWidth(ellipsis);
    int budget = maxWidthPx - ellipsisW;
    constexpr int charAdvance = 6;  // FONT5X7_GLYPH_WIDTH + 1

    size_t len = strlen(text);
    size_t keep = 0;
    int w = 0;
    while (keep < len && (keep + 1) < outSize)
    {
        if (w + charAdvance > budget)
            break;
        w += charAdvance;
        keep++;
    }

    size_t n = (keep < outSize - 1) ? keep : outSize - 1;
    memcpy(out, text, n);
    out[n] = '\0';
    size_t remaining = outSize - strlen(out) - 1;
    strncat(out, ellipsis, remaining);
}

void Screen::makeImageSizeStr(uint64_t size, char *buffer, size_t bufSize)
{
    const char *suffix = "B";
    uint64_t divisor = 1;

    if (size > 1073741824ULL)
    {
        divisor = 1073741824ULL;
        suffix = "G";
    }
    else if (size > 1048576ULL)
    {
        divisor = 1048576ULL;
        suffix = "M";
    }
    else if (size > 1024ULL)
    {
        divisor = 1024ULL;
        suffix = "k";
    }

    uint64_t whole = size / divisor;
    uint64_t remainder = size % divisor;
    unsigned tenths = (unsigned)((remainder * 10) / divisor);

    snprintf(buffer, bufSize, "%llu.%u%s", (unsigned long long)whole, tenths, suffix);
}

}  // namespace zuluide::display
