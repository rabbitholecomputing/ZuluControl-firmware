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

// Progress screen shown on the control panel while the ZuluControl board is
// itself receiving new firmware (over the web UI's HTTP POST or over I2C from
// the host -- both funnel through fw_upgrade.cpp's handle_uf2_block()). It is a
// port of ZuluSCSI-firmware's CopyScreen (lib/ZuluSCSI_UI_RP2MCU/CopyScreen.*):
// the same title + progress bar + percentage, retries/errors, elapsed/remaining
// time, transfer speed and remaining bytes, titled "Firmware Upgrade".
//
// It registers under DisplayScreenType::Copy (the slot CopyScreen occupies in
// ZuluSCSI's UI) and is driven entirely by DisplayControlTask(), which switches
// to it whenever fwupgrade_get_status() reports an upgrade in progress and back
// to Main when it ends. No input handling: buttons are inert during an upgrade.

#pragma once

#include "screen.h"

namespace zuluide::display {

class FirmwareUpgradeScreen : public Screen
{
public:
    explicit FirmwareUpgradeScreen(Framebuffer128x64 *fb) : Screen(fb) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::Copy; }

    void init(int index) override;
    void tick() override;
    void draw() override;

private:
    // Set to millis() the first tick that sees a block committed, so elapsed
    // time / speed / remaining-time are measured from the actual transfer
    // start (mirrors CopyScreen's _startTime latched on its first block).
    uint32_t _startMs = 0;
    bool _started = false;

    // Redraw throttling: refresh when the received-byte count advances or at
    // least every kRedrawMs so the elapsed/remaining clocks keep ticking even
    // while a flash sector is being erased (CopyScreen's MIN_UPDATE_TIME cadence).
    uint32_t _nextDrawMs = 0;
    uint64_t _lastBytes = 0xFFFFFFFFFFFFFFFFull;
};

}  // namespace zuluide::display
