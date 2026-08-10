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

// SSD1306 128x64 driver: just the command-byte sequences ported from
// Adafruit_SSD1306::begin()/display() (charge pump, addressing mode,
// contrast, segment/COM remap, display-on -- for the internal-charge-pump
// / 128x64 / SSD1306_SWITCHCAPVCC case ZuluSCSI's control board uses), not
// the Arduino class itself, since Adafruit_SSD1306/Adafruit_GFX/Wire
// aren't available in this bare pico-sdk build. Framebuffer pushes go out
// over i2c_master_dma.h's non-blocking DMA writer.

#pragma once

#include "framebuffer.h"
#include <hardware/i2c.h>
#include <cstdint>

namespace zuluide::display {

class Ssd1306Display
{
public:
    // Brings up the display (blocking -- runs once at boot, before any
    // other bus traffic, same as the original control.cpp's initScreenHardware()).
    bool Init(i2c_inst_t *i2c, unsigned int sdaPin, unsigned int sclPin, unsigned int baudrate, uint8_t addr);

    // Re-runs the panel's power-on command sequence (charge pump, addressing
    // window, contrast, display-on) without re-touching the already-initialized
    // I2C1 master/DMA -- a recovery path for a panel left mis-configured by a
    // glitch on the shared bus. Aborts any in-flight async push first, then
    // runs blocking like Init(). The caller must force a full framebuffer
    // re-push afterwards, since Reset() only reconfigures the controller and
    // does not touch GDDRAM.
    bool Reset();

    // Starts an async push of the framebuffer, one page (128 bytes) at a
    // time rather than all 1024 bytes in a single DMA burst -- see Poll()
    // for why. Returns false if a previous push is still in flight (call
    // Poll() until true).
    bool StartPush(const Framebuffer128x64 &fb);

    // Call every tick while a push is in flight; true once the whole frame
    // has finished. Services the current page's DMA transfer and, once it
    // completes, leaves the bus idle for this call -- the next page isn't
    // started until a *later* Poll() call. This gives the main loop's
    // GpioExpander::Poll() (which shares this same I2C1 bus/DMA channels
    // and runs before this in DisplayControlTask()) a guaranteed window
    // between pages instead of being locked out for the whole ~23ms/1024-byte
    // transfer -- each page is ~1/8th that, capping the encoder's blind
    // window at a single page instead of a whole frame.
    bool Poll();
    bool IsBusy();

    // Contrast / on-off, used by the blank screensaver style (mirrors
    // control.cpp's enableScreen()/SETCONTRAST dim support). Brief blocking
    // calls -- a 2-byte command, sub-millisecond, not on any hot path.
    void SetContrast(uint8_t contrast);
    void Enable(bool on);

private:
    uint8_t _addr = 0x3C;
    uint8_t _contrast = 0xCF;

    // Multi-page push state -- non-null buffer pointer means a push spanning
    // possibly-many Poll() calls is in progress; _pushOffset is the byte
    // offset of the next page still to send.
    const Framebuffer128x64 *_pushFb = nullptr;
    size_t _pushOffset = 0;

    bool startNextChunk();
    bool sendCommandsBlocking(const uint8_t *cmds, size_t n);
    bool sendInitSequence();
};

}  // namespace zuluide::display
