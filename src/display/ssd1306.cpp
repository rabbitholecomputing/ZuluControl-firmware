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

#include "ssd1306.h"
#include "i2c_master_dma.h"
#include <pico/time.h>

namespace zuluide::display {

namespace {
// SSD1306 command bytes, see datasheet -- names match Adafruit_SSD1306.h's
// #defines for anyone cross-referencing the original init sequence.
constexpr uint8_t SSD1306_MEMORYMODE = 0x20;
constexpr uint8_t SSD1306_SETCONTRAST = 0x81;
constexpr uint8_t SSD1306_CHARGEPUMP = 0x8D;
constexpr uint8_t SSD1306_SEGREMAP = 0xA0;
constexpr uint8_t SSD1306_DISPLAYALLON_RESUME = 0xA4;
constexpr uint8_t SSD1306_NORMALDISPLAY = 0xA6;
constexpr uint8_t SSD1306_SETMULTIPLEX = 0xA8;
constexpr uint8_t SSD1306_DISPLAYOFF = 0xAE;
constexpr uint8_t SSD1306_DISPLAYON = 0xAF;
constexpr uint8_t SSD1306_COMSCANDEC = 0xC8;
constexpr uint8_t SSD1306_SETDISPLAYOFFSET = 0xD3;
constexpr uint8_t SSD1306_SETDISPLAYCLOCKDIV = 0xD5;
constexpr uint8_t SSD1306_SETPRECHARGE = 0xD9;
constexpr uint8_t SSD1306_SETCOMPINS = 0xDA;
constexpr uint8_t SSD1306_SETVCOMDETECT = 0xDB;
constexpr uint8_t SSD1306_SETSTARTLINE = 0x40;
constexpr uint8_t SSD1306_DEACTIVATE_SCROLL = 0x2E;
constexpr uint8_t SSD1306_COLUMNADDR = 0x21;
constexpr uint8_t SSD1306_PAGEADDR = 0x22;
}  // namespace

bool Ssd1306Display::sendCommandsBlocking(const uint8_t *cmds, size_t n)
{
    if (!I2CMasterDmaStartWrite(_addr, 0x00, cmds, n))
        return false;
    bool ok = false;
    while (!I2CMasterDmaPoll(&ok))
    {
        tight_loop_contents();
    }
    return ok;
}

bool Ssd1306Display::Init(i2c_inst_t *i2c, unsigned int sdaPin, unsigned int sclPin, unsigned int baudrate, uint8_t addr)
{
    _addr = addr;

    if (!I2CMasterDmaInit(i2c, sdaPin, sclPin, baudrate))
        return false;

    // Sequence and values ported from Adafruit_SSD1306::begin(), specialized
    // for this board's fixed configuration: 128x64, SSD1306_SWITCHCAPVCC
    // (internal charge pump), each array sent as its own I2C transaction
    // exactly as ssd1306_command1()/ssd1306_commandList() do.
    const uint8_t init1[] = {SSD1306_DISPLAYOFF, SSD1306_SETDISPLAYCLOCKDIV, 0x80, SSD1306_SETMULTIPLEX};
    if (!sendCommandsBlocking(init1, sizeof(init1))) return false;
    const uint8_t heightArg[] = {Framebuffer128x64::HEIGHT - 1};
    if (!sendCommandsBlocking(heightArg, sizeof(heightArg))) return false;

    const uint8_t init2[] = {SSD1306_SETDISPLAYOFFSET, 0x00, (uint8_t)(SSD1306_SETSTARTLINE | 0x0), SSD1306_CHARGEPUMP};
    if (!sendCommandsBlocking(init2, sizeof(init2))) return false;
    const uint8_t chargePumpArg[] = {0x14};  // internal VCC
    if (!sendCommandsBlocking(chargePumpArg, sizeof(chargePumpArg))) return false;

    const uint8_t init3[] = {SSD1306_MEMORYMODE, 0x00, (uint8_t)(SSD1306_SEGREMAP | 0x1), SSD1306_COMSCANDEC};
    if (!sendCommandsBlocking(init3, sizeof(init3))) return false;

    // 128x64 internal-VCC values (Adafruit_SSD1306.cpp:596-598)
    const uint8_t comPins = 0x12;
    _contrast = 0xCF;

    const uint8_t compinsCmd[] = {SSD1306_SETCOMPINS, comPins};
    if (!sendCommandsBlocking(compinsCmd, sizeof(compinsCmd))) return false;
    const uint8_t contrastCmd[] = {SSD1306_SETCONTRAST, _contrast};
    if (!sendCommandsBlocking(contrastCmd, sizeof(contrastCmd))) return false;
    const uint8_t prechargeCmd[] = {SSD1306_SETPRECHARGE, 0xF1};
    if (!sendCommandsBlocking(prechargeCmd, sizeof(prechargeCmd))) return false;

    const uint8_t init5[] = {SSD1306_SETVCOMDETECT, 0x40, SSD1306_DISPLAYALLON_RESUME,
                              SSD1306_NORMALDISPLAY, SSD1306_DEACTIVATE_SCROLL, SSD1306_DISPLAYON};
    if (!sendCommandsBlocking(init5, sizeof(init5))) return false;

    // Set the addressing window once (full screen, all 8 pages / 128
    // columns) so StartPush() can just stream the buffer thereafter,
    // matching Adafruit_SSD1306::display()'s dlist1 + column range.
    const uint8_t pageAddr[] = {SSD1306_PAGEADDR, 0, 0xFF};
    if (!sendCommandsBlocking(pageAddr, sizeof(pageAddr))) return false;
    const uint8_t colAddr[] = {SSD1306_COLUMNADDR, 0, Framebuffer128x64::WIDTH - 1};
    if (!sendCommandsBlocking(colAddr, sizeof(colAddr))) return false;

    return true;
}

bool Ssd1306Display::StartPush(const Framebuffer128x64 &fb)
{
    return I2CMasterDmaStartWrite(_addr, 0x40, fb.buffer, Framebuffer128x64::SIZE_BYTES);
}

bool Ssd1306Display::Poll()
{
    return I2CMasterDmaPoll();
}

bool Ssd1306Display::IsBusy()
{
    return I2CMasterDmaIsBusy();
}

void Ssd1306Display::SetContrast(uint8_t contrast)
{
    _contrast = contrast;
    const uint8_t cmd[] = {SSD1306_SETCONTRAST, contrast};
    sendCommandsBlocking(cmd, sizeof(cmd));
}

void Ssd1306Display::Enable(bool on)
{
    const uint8_t cmd[] = {SSD1306_SETCONTRAST, (uint8_t)(on ? _contrast : 0)};
    sendCommandsBlocking(cmd, sizeof(cmd));
}

}  // namespace zuluide::display
