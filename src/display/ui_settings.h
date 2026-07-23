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

// Small process-wide UI settings shared between the ZuluIDE screens and the
// display task. This project has no persisted-settings store yet (same note
// as screen_saver.h's idle-timeout constant), so these live only in RAM and
// reset to their defaults on every boot -- they're the runtime state the
// Settings screen edits and the Browse screen / screen saver read, not a
// configuration file. Kept as free functions over a translation-unit-local
// state block rather than a struct passed around, mirroring how the rest of
// the display module reaches shared globals (webui_data.h's accessors,
// g_device_type).

#pragma once

#include "screen_saver.h"

namespace zuluide::display {

// Browse-screen rotary scroll step: how many list entries one rotary detent
// moves. Cycles 1 -> 10 -> 50 -> 1 (the Settings screen and the Browse menu
// both set it). Default 1.
int GetScrollStep();
void SetScrollStep(int step);
void CycleScrollStep();

// Which screen saver style the controller should use. Default Random (task
// requirement). The display task applies this to its ScreenSaverController
// every tick, so a change from the Settings screen takes effect on the next
// activation without any direct coupling between the two.
ScreenSaverType GetConfiguredScreenSaver();
void SetConfiguredScreenSaver(ScreenSaverType style);
void CycleScreenSaver();

// Short display name for a screen saver style (for the Settings screen).
const char *ScreenSaverName(ScreenSaverType style);

// "Turn on screen saver now" one-shot request: a screen menu sets it, the
// display task consumes it once and force-activates the saver. Decoupled the
// same way as GetConfiguredScreenSaver() so screens don't need a handle to
// the controller.
void RequestScreenSaverNow();
bool ConsumeScreenSaverNowRequest();

}  // namespace zuluide::display
