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

// Core-0 non-blocking scheduler for the I2C display/control panel: input
// polling, screen/screensaver ticking, and rate-limited DMA framebuffer
// pushes. Follows the same call-once-per-main-loop-iteration idiom already
// used twice in this codebase (ZuluSCSI-firmware's platform_poll() calling
// zuluWebUITask(), and this project's own
// zuluide::i2c::client::ProcessMessages() call in main.cpp's while(true)
// loop) -- never blocks for more than a few microseconds.

#pragma once

namespace zuluide::display {

// Brings up the SSD1306 + GPIO-expander bus (i2c1) and shows the splash
// screen. Call once from main(), after start_multicore_i2c() (order
// doesn't matter functionally -- i2c1 is independent of the i2c0 slave
// link -- but matches the plan's placement). Returns false if the display
// didn't ACK on the bus (GPIO-expander absence is tolerated; buttons/
// rotary just won't do anything -- see the NoControlsError placeholder
// note for why this iteration doesn't add a dedicated fallback screen).
bool InitDisplayControl();

// Call once per iteration of main()'s while(true) loop, unconditionally
// (not gated on the WiFi/I2C `programState` switch, so the splash sequence
// and connection status can render before State::Normal is reached).
void DisplayControlTask();

// Renders and pushes the "Firmware Upgrade" progress screen synchronously,
// once (subject to an internal throttle), reading fwupgrade_get_status().
// A no-op unless an upgrade is active. Meant to be called from BOTH core0's
// main loop (which drives the I2C upgrade path) AND, crucially, the lwIP
// background context that services a web (HTTP POST) upload -- during which
// the main loop is starved, so DisplayControlTask() alone would leave the
// bar frozen. A reentrancy/bus-ownership guard makes the two callers safe:
// whichever enters first owns i2c1 for its blocking push; the other returns
// immediately. Safe to call before InitDisplayControl() (no-op) and when no
// panel is present.
void PumpFirmwareUpgradeDisplay();

}  // namespace zuluide::display
