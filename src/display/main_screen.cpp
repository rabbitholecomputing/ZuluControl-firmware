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

#include "main_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include "../ZuluControlI2CClient.h"
#include <cstdio>
#include <cstring>

namespace zuluide::display {

namespace {

constexpr int DEVICES_PER_PAGE = 8;
constexpr int DEVICES_PER_COLUMN = 4;

}  // namespace

void MainScreen::init(int index)
{
    Screen::init(index);
    _data->Refresh();

    if (index >= 0 && index < DisplayData::MAX_DEVICES)
        _selectedIndex = index;
    else
        _selectedIndex = -1;

    selectFirstPresentIfNeeded();
    forceDraw();
}

void MainScreen::selectFirstPresentIfNeeded()
{
    if (_selectedIndex >= 0)
        return;

    for (int i = 0; i < DisplayData::MAX_DEVICES; i++)
    {
        if (_data->GetDevice(i)->present)
        {
            _selectedIndex = i;
            break;
        }
    }
}

void MainScreen::tick()
{
    _data->Refresh();
    selectFirstPresentIfNeeded();
    forceDraw();
    Screen::tick();
}

void MainScreen::draw()
{
    _fb->drawText(0, 0, "SCSI Map");

    // Dynamic page count: how many DEVICES_PER_PAGE-sized pages actually
    // hold a present device, not a fixed constant (see header comment).
    int highestPresent = 0;
    for (int i = 0; i < DisplayData::MAX_DEVICES; i++)
    {
        if (_data->GetDevice(i)->present)
            highestPresent = i;
    }
    int totalPages = (highestPresent / DEVICES_PER_PAGE) + 1;

    int page = (_selectedIndex > -1) ? (_selectedIndex / DEVICES_PER_PAGE) : 0;
    char pageText[24];
    snprintf(pageText, sizeof(pageText), " (%d/%d)", page + 1, totalPages);
    _fb->drawText(Framebuffer128x64::textWidth("SCSI Map"), 0, pageText);

    _fb->drawHLine(0, 10, 112);

    if (_data->SdPresent())
        _fb->drawBitmap(115, 0, icon_sd, 12, 12);
    else
        _fb->drawBitmap(115, 0, icon_nosd, 12, 12);

    int deviceOffset = page * DEVICES_PER_PAGE;
    int xOffset = 0, yOffset = 13;
    for (int i = 0; i < DEVICES_PER_PAGE; i++)
    {
        if (i == DEVICES_PER_COLUMN)
        {
            xOffset = 64;
            yOffset = 13;
        }

        drawDeviceItem(xOffset, yOffset, i + deviceOffset);
        yOffset += 13;
    }
}

void MainScreen::drawDeviceItem(int x, int y, int deviceIndex)
{
    const DeviceInfo *dev = _data->GetDevice(deviceIndex);
    if (!dev)
        return;

    char idText[12];
    snprintf(idText, sizeof(idText), "%d", deviceIndex);
    _fb->drawText(x + 10, y + 2, idText);

    if (_selectedIndex == deviceIndex)
        _fb->drawBitmap(x, y + 1, icon_select, 8, 8);

    if (dev->present)
    {
        _fb->drawBitmap(x + 36, y, IconForDeviceType(dev->type), 12, 12);

        // Hidden placeholder: original also overlays icon_rom/icon_raw
        // here (map->IsRom/map->IsRaw) -- not present in the status JSON yet.

        _fb->drawBitmap(x + 22, y, dev->ejected ? icon_ledsemi : icon_ledon, 12, 12);
    }
    else
    {
        _fb->drawBitmap(x + 22, y, icon_ledoff, 12, 12);
    }
}

void MainScreen::shortRotaryPress()
{
    if (_selectedIndex < 0)
        return;
    const DeviceInfo *dev = _data->GetDevice(_selectedIndex);
    if (!dev || !dev->present)
        return;
    ChangeScreen(DisplayScreenType::Info, _selectedIndex);
}

void MainScreen::shortEjectPress()
{
    if (_selectedIndex < 0)
        return;
    const DeviceInfo *dev = _data->GetDevice(_selectedIndex);
    if (!dev || !dev->present)
        return;
    ChangeScreen(DisplayScreenType::Browse, _selectedIndex);
}

void MainScreen::longUserPress()
{
    ChangeScreen(DisplayScreenType::Settings, 0);
}

void MainScreen::longEjectPress()
{
    if (_selectedIndex < 0)
        return;
    const DeviceInfo *dev = _data->GetDevice(_selectedIndex);
    if (!dev || !dev->present)
        return;

    if (_data->GetDeviceType() == DeviceType::ZuluSCSI)
    {
        uint8_t scsiId = (uint8_t)dev->id;
        zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_EJECT_IMAGE, &scsiId, 1);
    }
    else
    {
        zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_EJECT_IMAGE);
    }
    ShowMessage("-- Info --", "Ejecting...", dev->image[0] ? dev->image : "", DisplayScreenType::Main, _selectedIndex, 1000);
}

void MainScreen::rotaryChange(int direction)
{
    if (_selectedIndex < 0)
        return;

    bool found = false;
    int i;

    if (direction == 1)
    {
        for (i = _selectedIndex + 1; i < DisplayData::MAX_DEVICES; i++)
        {
            if (_data->GetDevice(i)->present)
            {
                _selectedIndex = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (i = 0; i < _selectedIndex; i++)
            {
                if (_data->GetDevice(i)->present)
                {
                    _selectedIndex = i;
                    found = true;
                    break;
                }
            }
        }
    }
    else
    {
        for (i = _selectedIndex - 1; i >= 0; i--)
        {
            if (_data->GetDevice(i)->present)
            {
                _selectedIndex = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (i = DisplayData::MAX_DEVICES - 1; i >= _selectedIndex; i--)
            {
                if (_data->GetDevice(i)->present)
                {
                    _selectedIndex = i;
                    found = true;
                    break;
                }
            }
        }
    }

    if (found)
        forceDraw();
}

}  // namespace zuluide::display
