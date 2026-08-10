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

// Rotary quadrature + 3-button decode for the I2C GPIO-expander half of the
// display/control panel. Ported from ZuluSCSI-firmware's
// lib/ZuluSCSI_UI_RP2MCU/control.cpp (rotary_transition_lut, checkButton(),
// processButtons(), processRotaryEncoder(), updateRotary()) against
// i2c_master_dma.h's short-transaction primitives instead of Arduino Wire.
// Same expander bit layout as the original hardware (EXP_ROT_A_PIN=0,
// EXP_ROT_B_PIN=1, EXP_EJECT_PIN=2, EXP_INSERT_PIN=3, EXP_ROT_PIN=5,
// control.h:41-45), active-low (button pressed pulls its input bit low).

#pragma once

#include <cstdint>

namespace zuluide::display {

enum class Button
{
    Eject = 0,
    Insert = 1,
    Rotary = 2,
    Count = 3
};

struct InputEvents
{
    bool shortPress[3] = {false, false, false};
    bool longPress[3] = {false, false, false};
    int rotaryDirection = 0;  // -1, 0, or +1 this poll
};

class GpioExpander
{
public:
    explicit GpioExpander(uint8_t addr) : _addr(addr) {}

    void SetReverseRotary(bool reverse) { _reverseRotary = reverse; }

    // Reads the expander's input register over the shared i2c_master_dma
    // bus and decodes rotary + button state into `out`. Safe to call every
    // main-loop tick -- a short, bounded (5ms timeout) transaction. Returns
    // false (leaving `out` default-initialized) if the read failed/timed out.
    bool Poll(InputEvents &out);

private:
    uint8_t _addr;
    bool _reverseRotary = false;

    // Rotary decode state (mirrors control.cpp's g_rotary_state/g_going_cw/g_tick_count).
    uint8_t _rotaryState = 0;
    bool _goingCw = true;
    int _tickCount = 0;
    static constexpr int kNumberOfTicks = 1;  // matches control.cpp's fixed g_number_of_ticks = 1

    // Button decode state (mirrors control.cpp's g_button3StartTime/g_button3Press/g_waitingForButton3Release).
    bool _buttonPressed[3] = {false, false, false};
    bool _waitingForRelease[3] = {false, false, false};
    uint32_t _buttonStartTime[3] = {0, 0, 0};

    int decodeRotary(uint8_t inputByte);
    void decodeButton(uint8_t inputByte, int index, uint8_t pinBit, InputEvents &out);
};

}  // namespace zuluide::display
