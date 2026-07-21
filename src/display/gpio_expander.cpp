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

#include "gpio_expander.h"
#include "i2c_master_dma.h"
#include <pico/time.h>

namespace zuluide::display {

namespace {

// Expander bit positions, same layout as ZuluSCSI-firmware's control.h:41-45.
constexpr uint8_t EXP_ROT_A_PIN = 0;
constexpr uint8_t EXP_ROT_B_PIN = 1;
constexpr uint8_t EXP_EJECT_PIN = 2;
constexpr uint8_t EXP_INSERT_PIN = 3;
constexpr uint8_t EXP_ROT_PIN = 5;

enum RotaryDirection
{
    ROTARY_DIR_NONE = 0,
    ROTARY_DIR_CW = 0x10,
    ROTARY_DIR_CCW = 0x20
};

enum RotaryState
{
    ROTARY_TICK_000 = 0,
    ROTARY_LAST_CW_001,
    ROTARY_START_CW_010,
    ROTARY_CONT_CW_011,
    ROTARY_START_CCW_100,
    ROTARY_LAST_CCW_101,
    ROTARY_CONT_CCW_110,
};

// Verbatim port of ZuluSCSI-firmware's control.cpp:79-95 quadrature Gray-code
// transition table.
const uint8_t rotary_transition_lut[7][4] = {
    {ROTARY_TICK_000, ROTARY_START_CW_010, ROTARY_START_CCW_100, ROTARY_TICK_000},
    {ROTARY_CONT_CW_011, ROTARY_TICK_000, ROTARY_LAST_CW_001, ROTARY_TICK_000 | ROTARY_DIR_CW},
    {ROTARY_CONT_CW_011, ROTARY_START_CW_010, ROTARY_TICK_000, ROTARY_TICK_000},
    {ROTARY_CONT_CW_011, ROTARY_START_CW_010, ROTARY_LAST_CW_001, ROTARY_TICK_000},
    {ROTARY_CONT_CCW_110, ROTARY_TICK_000, ROTARY_START_CCW_100, ROTARY_TICK_000},
    {ROTARY_CONT_CCW_110, ROTARY_LAST_CCW_101, ROTARY_TICK_000, ROTARY_TICK_000 | ROTARY_DIR_CCW},
    {ROTARY_CONT_CCW_110, ROTARY_LAST_CCW_101, ROTARY_START_CCW_100, ROTARY_TICK_000},
};

uint32_t millis()
{
    return to_ms_since_boot(get_absolute_time());
}

}  // namespace

int GpioExpander::decodeRotary(uint8_t inputByte)
{
    uint8_t chanA = 1 & (inputByte >> EXP_ROT_A_PIN);
    uint8_t chanB = 1 & (inputByte >> EXP_ROT_B_PIN);
    uint8_t rotaryInputState = (chanB << 1) | chanA;
    _rotaryState = rotary_transition_lut[_rotaryState & 0x0F][rotaryInputState];
    RotaryDirection direction = (RotaryDirection)(_rotaryState & 0x30);

    int fired = 0;
    if (_goingCw)
    {
        if (direction == ROTARY_DIR_CW)
        {
            if (_tickCount < kNumberOfTicks - 1)
            {
                _tickCount++;
            }
            else
            {
                fired = -1;
                _tickCount = 0;
            }
        }
        else if (direction == ROTARY_DIR_CCW)
        {
            _tickCount = 1;
            if (_tickCount > kNumberOfTicks - 1)
            {
                fired = 1;
            }
            _goingCw = false;
        }
    }
    else
    {
        if (direction == ROTARY_DIR_CCW)
        {
            if (_tickCount < kNumberOfTicks - 1)
            {
                _tickCount++;
            }
            else
            {
                fired = 1;
                _tickCount = 0;
            }
        }
        else if (direction == ROTARY_DIR_CW)
        {
            _tickCount = 1;
            if (_tickCount > kNumberOfTicks - 1)
            {
                fired = -1;
            }
            _goingCw = true;
        }
    }

    if (fired != 0 && _reverseRotary)
        fired = -fired;

    return fired;
}

void GpioExpander::decodeButton(uint8_t inputByte, int index, uint8_t pinBit, InputEvents &out)
{
    uint32_t now = millis();
    bool value = ((1 << pinBit) & inputByte) == 0;  // active-low

    if (!_buttonPressed[index])
    {
        if (value)
        {
            _buttonPressed[index] = true;
            _waitingForRelease[index] = true;
            _buttonStartTime[index] = now;
        }
    }
    else if (now - _buttonStartTime[index] > 100)
    {
        if (_waitingForRelease[index] && !value)
        {
            _waitingForRelease[index] = false;
            _buttonPressed[index] = false;

            if (now - _buttonStartTime[index] > 1000)
                out.longPress[index] = true;
            else
                out.shortPress[index] = true;
        }
    }
}

bool GpioExpander::Poll(InputEvents &out)
{
    out = InputEvents{};

    uint8_t reg = 0;
    uint8_t inputByte = 0xFF;
    absolute_time_t until = make_timeout_time_ms(5);
    if (!I2CMasterWriteThenRead(_addr, &reg, 1, &inputByte, 1, until))
        return false;

    out.rotaryDirection = decodeRotary(inputByte);
    decodeButton(inputByte, (int)Button::Eject, EXP_EJECT_PIN, out);
    decodeButton(inputByte, (int)Button::Insert, EXP_INSERT_PIN, out);
    decodeButton(inputByte, (int)Button::Rotary, EXP_ROT_PIN, out);

    return true;
}

}  // namespace zuluide::display
