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

// Generic non-blocking DMA I2C master for the new display/control panel's
// bus (i2c1). Generalizes the chained header+data DMA technique already
// used for ZuluControl-firmware-upgrade transfers in
// related/ZuluIDE-firmware/lib/ZuluControl/include/zuluide/i2c/i2c_master_dma.h/.cpp
// (chained DMA channels, STOP-bit-in-last-word trick, poll-until-done-or-abort)
// -- but generalized to a single caller-supplied control byte + raw payload,
// since this bus speaks the SSD1306 command/data protocol (control byte
// 0x00 or 0x40), not the WebUI [cmd][len][payload] framing that driver was
// built for.
//
// Unlike that reference (which blocks the caller until the transfer
// completes), StartWrite() here returns immediately and the caller polls
// Poll() from the main loop -- needed because a full 1024-byte framebuffer
// push must not stall core0's lwIP/httpd servicing.

#pragma once

#include <hardware/i2c.h>
#include <cstdint>
#include <cstddef>

namespace zuluide::display {

// Must be called once before any other function, with the i2c1 instance
// this display+expander bus uses. Claims two DMA channels via
// dma_claim_unused_channel() (safe here since nothing else in
// ZuluControl-firmware claims DMA channels yet).
bool I2CMasterDmaInit(i2c_inst_t *i2c, unsigned int sdaPin, unsigned int sclPin, unsigned int baudrate);

// Maximum payload bytes per write (matches Framebuffer128x64::SIZE_BYTES,
// the largest single transfer this bus ever needs to make).
constexpr size_t I2C_DISPLAY_MAX_PAYLOAD = 1024;

// Starts an asynchronous write of `controlByte` followed by `payload`
// (`length` bytes) to `addr`. Returns false if a previous write is still
// in flight (call Poll() until it returns true first) or length is 0/too
// large. Returns immediately -- does not wait for the transfer to finish.
bool I2CMasterDmaStartWrite(uint8_t addr, uint8_t controlByte, const uint8_t *payload, size_t length);

// Call every main-loop tick while a write is in flight. Returns true once
// the transfer has finished (successfully, aborted, or timed out) -- at
// which point IsBusy() is false and, if out_ok is non-null, *out_ok
// reports success. Returns false while still busy (nothing to report yet).
bool I2CMasterDmaPoll(bool *out_ok = nullptr);

bool I2CMasterDmaIsBusy();

// Short blocking-with-timeout raw I2C register-read transaction for the
// GPIO-expander's poll (write the register pointer, then read its value)
// -- a couple of bytes at 400kHz is a handful of microseconds, not worth
// DMA-chaining (see i2c_master_dma.cpp). Uses a repeated START between the
// write and the read (no STOP in between) instead of two separate
// transactions, both to shave off the extra STOP/START bit-time overhead
// and to avoid a window where the expander's input latch could change
// between them -- the standard I2C "register read" idiom, and part of
// making the rotary encoder more responsive.
bool I2CMasterWriteThenRead(uint8_t addr, const uint8_t *writeData, size_t writeLen,
                            uint8_t *readData, size_t readLen, absolute_time_t until);

}  // namespace zuluide::display
