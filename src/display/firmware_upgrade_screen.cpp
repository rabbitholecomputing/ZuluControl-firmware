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

#include "firmware_upgrade_screen.h"
#include "../fw_upgrade.h"
#include <pico/time.h>
#include <cstdio>

namespace zuluide::display {

namespace {

// Progress-bar geometry, identical to CopyScreen's ProgressBar bounds
// (Rectangle{{0,18},{90,11}}) and its fill math (interior width = 90-0-2).
constexpr int kBarX = 0;
constexpr int kBarY = 18;
constexpr int kBarW = 90;
constexpr int kBarH = 11;
constexpr int kBarFillMax = kBarW - 2;  // 88px of interior travel

constexpr uint32_t kRedrawMs = 500;

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }

// "HH:MM:SS", hours hard-capped at 99 -- a port of Screen::makeTimeStr().
void makeTimeStr(uint32_t seconds, char *buf, size_t n)
{
    uint32_t hours = seconds / 3600;
    if (hours > 99) hours = 99;
    uint32_t rem = seconds - (hours * 3600);
    uint32_t mins = rem / 60;
    uint32_t secs = rem - (mins * 60);
    snprintf(buf, n, "%02lu:%02lu:%02lu",
             (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
}

}  // namespace

void FirmwareUpgradeScreen::init(int index)
{
    Screen::init(index);
    _startMs = 0;
    _started = false;
    _nextDrawMs = 0;
    _lastBytes = 0xFFFFFFFFFFFFFFFFull;
    forceDraw();
}

void FirmwareUpgradeScreen::tick()
{
    FwUpgradeStatus st;
    fwupgrade_get_status(&st);

    uint32_t now = millis();

    // Latch the transfer start once real data begins flowing.
    if (!_started && st.bytes_received > 0)
    {
        _started = true;
        _startMs = now;
    }

    if (st.bytes_received != _lastBytes || (int32_t)(now - _nextDrawMs) >= 0)
    {
        _lastBytes = st.bytes_received;
        _nextDrawMs = now + kRedrawMs;
        forceDraw();
    }

    Screen::tick();
}

void FirmwareUpgradeScreen::draw()
{
    FwUpgradeStatus st;
    fwupgrade_get_status(&st);

    char buf[24];

    // Title + separator line (CopyScreen banner + drawLine at y=10).
    _fb->drawText(0, 0, "Firmware Upgrade");
    _fb->drawHLine(0, 10, Framebuffer128x64::WIDTH);

    // Clamp received to the known total (retried I2C chunks or a slightly
    // off Content-Length shouldn't push the bar past full).
    uint64_t received = st.bytes_received;
    if (st.total_bytes > 0 && received > st.total_bytes)
        received = st.total_bytes;

    // Progress bar (outline + proportional fill).
    _fb->drawRectOutline(kBarX, kBarY, kBarW, kBarH);
    int fill = 0;
    if (st.total_bytes > 0 && received > 0)
    {
        fill = (int)((uint64_t)kBarFillMax * received / st.total_bytes);
        if (fill > kBarFillMax) fill = kBarFillMax;
    }
    if (fill > 0)
        _fb->fillRect(kBarX + 1, kBarY + 1, fill, kBarH - 2);

    // Percentage (tenths), right-aligned into the space beside the bar.
    uint32_t pct10 = st.total_bytes
                         ? (uint32_t)(received * 1000 / st.total_bytes)
                         : 0;
    if (pct10 > 1000) pct10 = 1000;
    snprintf(buf, sizeof(buf), "%lu.%lu%%",
             (unsigned long)(pct10 / 10), (unsigned long)(pct10 % 10));
    printRightAligned(buf, Framebuffer128x64::WIDTH, 20);

    // Retries / Errors (CopyScreen's showRetriesAndErrors row).
    snprintf(buf, sizeof(buf), "Ret: %lu", (unsigned long)st.retries);
    _fb->drawText(0, 32, buf);
    snprintf(buf, sizeof(buf), "Err: %lu", (unsigned long)st.errors);
    _fb->drawText(68, 32, buf);

    uint32_t elapsedMs = _started ? (millis() - _startMs) : 0;

    // Transfer speed (bytes/sec, averaged over the elapsed time) at bottom-left.
    uint64_t speed = elapsedMs ? (received * 1000 / elapsedMs) : 0;
    makeImageSizeStr(speed, buf, sizeof(buf));
    size_t len = 0;
    while (buf[len]) len++;
    snprintf(buf + len, sizeof(buf) - len, "Bs");
    _fb->drawText(0, 45, buf);

    // Elapsed time (E:) top-right of the stats block.
    makeTimeStr(elapsedMs / 1000, buf + 2, sizeof(buf) - 2);
    buf[0] = 'E'; buf[1] = ':';
    _fb->drawText(68, 45, buf);

    // Remaining bytes (R:) at bottom-left.
    uint64_t remainingBytes = (st.total_bytes > received) ? (st.total_bytes - received) : 0;
    char sizeBuf[16];
    makeImageSizeStr(remainingBytes, sizeBuf, sizeof(sizeBuf));
    snprintf(buf, sizeof(buf), "R: %s", sizeBuf);
    _fb->drawText(0, 56, buf);

    // Remaining time (R:) at bottom-right, linear-extrapolated from progress.
    uint32_t remSec = 0;
    if (_started && received > 0 && st.total_bytes >= received)
    {
        uint64_t remMs = (uint64_t)elapsedMs * (st.total_bytes - received) / received;
        remSec = (uint32_t)(remMs / 1000);
    }
    makeTimeStr(remSec, buf + 2, sizeof(buf) - 2);
    buf[0] = 'R'; buf[1] = ':';
    _fb->drawText(68, 56, buf);
}

}  // namespace zuluide::display
