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

#include "screen_saver.h"
#include "icons.h"
#include <pico/time.h>
#include <cstdlib>
#include "../ZuluControl_config.h"

namespace zuluide::display {

namespace {

uint32_t millis()
{
    return to_ms_since_boot(get_absolute_time());
}

bool g_randSeeded = false;

// Arduino-style random(n): returns [0, n).
int randRange(int n)
{
    if (!g_randSeeded)
    {
        srand(millis());
        g_randSeeded = true;
    }
    if (n <= 0)
        return 0;
    return rand() % n;
}

}  // namespace

const uint8_t *ScreenSaverController::iconByIndex(int index)
{
    // Verbatim port of ScreenSaver.cpp's GetIconByIndex().
    switch (index)
    {
        case 0: return icon_cd;
        case 1: return icon_floppy;
        case 2: return icon_removable;
        case 3: return icon_fixed;
        case 4: return icon_mo;
        case 5: return icon_network;
        case 6: return icon_tape;
        case 7: return icon_zip;
        case 8: return icon_rom;
        case 9: return icon_sd;
        case 10: return icon_nosd;
    }
    return icon_cd;
}

void ScreenSaverController::enter()
{
    _currentSelection = _configuredStyle;
    if (_currentSelection == ScreenSaverType::Random)
    {
        // 6 concrete styles, values 1..6 in ScreenSaverType (Blank..Lightspeed).
        _currentSelection = (ScreenSaverType)(1 + randRange(TOTAL_SCREEN_SAVER_STYLES));
    }

    for (int i = 0; i < MAX_OBJECT_MANY; i++)
        _particleActive[i] = false;

    if (_display)
        _display->Enable(_currentSelection != ScreenSaverType::Blank);

    _nextChangeMs = 0;
}

void ScreenSaverController::exit()
{
    if (_display)
        _display->Enable(true);
}

bool ScreenSaverController::draw()
{
    if (_currentSelection == ScreenSaverType::Blank)
        return false;

    _fb->clear();

    switch (_currentSelection)
    {
        case ScreenSaverType::RandomPositionLogo:
        {
            _scrX = randRange(64);
            _scrY = randRange(33);
            switch(g_device_type)
            {
                case zulucontrol::config::DeviceType::ZuluSCSI :
                    _fb->drawBitmap(_scrX, _scrY, icon_zulu_scsi_logo_mini, 64, 31);
                    break;
                default:
                    _fb->drawBitmap(_scrX, _scrY, icon_zulu_control_logo_mini, 64, 31);
            }

            break;
        }
        case ScreenSaverType::BouncingLogo:
        {
            _scrX += _dirX ? 1 : -1;
            _scrY += _dirY ? 1 : -1;
            if (_scrX > 62 || _scrX < 1)
                _dirX = !_dirX;
            if (_scrY > 30 || _scrY < 1)
                _dirY = !_dirY;
              switch(g_device_type)
            {
                case zulucontrol::config::DeviceType::ZuluSCSI :
                    _fb->drawBitmap(_scrX, _scrY, icon_zulu_scsi_logo_mini, 64, 31);
                    break;
                default:
                    _fb->drawBitmap(_scrX, _scrY, icon_zulu_control_logo_mini, 64, 31);
            }
            break;
        }
        case ScreenSaverType::HorizontalScrollingIcons:
        case ScreenSaverType::VerticalRainingIcons:
        {
            int index = -1;
            for (int i = 0; i < MAX_OBJECT_FEW; i++)
            {
                if (!_particleActive[i])
                {
                    index = i;
                    break;
                }
            }

            if (index > -1 && randRange(10) == 0)
            {
                _particleActive[index] = true;
                if (_currentSelection == ScreenSaverType::HorizontalScrollingIcons)
                {
                    _points[index].set(-12, (float)randRange(54));
                    _vectors[index].set((float)(randRange(3) + 1), 0);
                }
                else
                {
                    _points[index].set((float)randRange(116), -12);
                    _vectors[index].set(0, (float)(randRange(3) + 1));
                }
                _iconIdx[index] = (uint8_t)randRange(11);
            }

            for (int i = 0; i < MAX_OBJECT_FEW; i++)
            {
                if (_particleActive[i])
                {
                    _points[i] += _vectors[i];
                    if (_currentSelection == ScreenSaverType::HorizontalScrollingIcons)
                    {
                        if (_points[i].x > 127)
                            _particleActive[i] = false;
                    }
                    else
                    {
                        if (_points[i].y > 63)
                            _particleActive[i] = false;
                    }
                }
            }

            for (int i = 0; i < MAX_OBJECT_FEW; i++)
            {
                if (_particleActive[i])
                    _fb->drawBitmap((int)_points[i].x, (int)_points[i].y, iconByIndex(_iconIdx[i]), 12, 12);
            }
            break;
        }
        case ScreenSaverType::Lightspeed:
        {
            int index = -1;
            for (int i = 0; i < MAX_OBJECT_MANY; i++)
            {
                if (!_particleActive[i])
                {
                    index = i;
                    break;
                }
            }

            if (index > -1)
            {
                _particleActive[index] = true;
                _particleSize[index] = (uint8_t)randRange(4);
                _points[index].set(64, 32);

                float tX, tY;
                if (randRange(2) == 0)
                {
                    tX = (randRange(2) == 0) ? 127 : 0;
                    tY = (float)(randRange(76) - 12);
                }
                else
                {
                    tX = (float)(randRange(140) - 12);
                    tY = (randRange(2) == 0) ? 63 : 0;
                }

                Vec2D vec(_points[index].x, _points[index].y, tX, tY);
                _vectors[index].set(vec.normalized());
                _vectors[index] *= 0.5f;
                _iconIdx[index] = (uint8_t)randRange(11);
            }

            for (int i = 0; i < MAX_OBJECT_MANY; i++)
            {
                if (_particleActive[i])
                {
                    _vectors[i] *= 1.1f;
                    _points[i] += _vectors[i];
                    if (_points[i].x > 127 || _points[i].x < -12)
                        _particleActive[i] = false;
                    if (_points[i].y > 63 || _points[i].y < -12)
                        _particleActive[i] = false;
                }
            }

            for (int i = 0; i < MAX_OBJECT_MANY; i++)
            {
                if (_particleActive[i])
                {
                    int px = (int)_points[i].x, py = (int)_points[i].y;
                    if (_particleSize[i] == 0)
                        _fb->fillRect(px - 1, py - 1, 3, 3);  // approximates Adafruit_GFX's fillCircle(r=1)
                    else
                        _fb->setPixel(px, py);
                }
            }
            break;
        }
        default:
            break;
    }

    int delay = 0;
    switch (_currentSelection)
    {
        case ScreenSaverType::RandomPositionLogo: delay = 5000; break;
        case ScreenSaverType::BouncingLogo: delay = 200; break;
        case ScreenSaverType::HorizontalScrollingIcons:
        case ScreenSaverType::VerticalRainingIcons: delay = 40; break;
        case ScreenSaverType::Lightspeed: delay = 40; break;
        default: break;
    }
    _nextChangeMs = millis() + delay;
    return true;
}

bool ScreenSaverController::Tick()
{
    uint32_t now = millis();
    bool inputThisTick = _userInputDetected;
    _userInputDetected = false;

    if (!_active)
    {
        if (!_timerStarted)
        {
            if (!inputThisTick)
            {
                _timerStarted = true;
                _timerStartMs = now;
            }
        }
        else if (inputThisTick)
        {
            _timerStarted = false;
        }
        else if (now - _timerStartMs > _idleTimeoutMs)
        {
            _active = true;
            _timerStarted = false;
            enter();
        }
        return false;
    }

    if (inputThisTick)
    {
        _active = false;
        exit();
        return false;
    }

    if ((int32_t)(now - _nextChangeMs) < 0)
        return false;

    return draw();
}

}  // namespace zuluide::display
