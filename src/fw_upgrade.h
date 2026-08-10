/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

#pragma once

#include <stdint.h>
#include "lwip/apps/httpd.h"

// This module defines handlers for HTTP POST requests used for firmware upgrade

// Called from httpd_post_begin()
err_t fwupgrade_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd);

// Called from httpd_post_receive_data
err_t fwupgrade_post_receive_data(void *connection, struct pbuf *p);

// Called from http_post_finished
void fwupgrade_post_finished(void *connection, char *response_uri, u16_t response_uri_len);

// Snapshot of firmware-upgrade progress, for the local control-panel display.
// Both the HTTP (web) and I2C upgrade paths funnel through handle_uf2_block(),
// so this single set of counters covers both. Populated by fwupgrade_get_status();
// safe to read from core0's display task (the fields are plain 32-bit words).
struct FwUpgradeStatus
{
    bool     active;         // an upgrade is underway: begin seen, not yet finished/aborted
    bool     via_http;       // true = web (HTTP POST) upload, false = I2C from the host
    // Full-file upload progress measured in *bytes actually received*, not the
    // UF2's own block numbering. A universal .uf2 concatenates two independently
    // numbered family images (block_no restarts, num_blocks differs, and the
    // blocks are interleaved -- see universal_binary/CMakeLists.txt), so a
    // block-number-based bar can't sweep cleanly. Byte counters are monotonic
    // and layout-agnostic, so the bar tracks the whole file to 100% for any
    // image. (The flashing logic still keys off the family-matching subset.)
    uint64_t bytes_received; // firmware bytes received so far
    uint64_t total_bytes;    // total file size; 0 while still unknown
    uint32_t retries;        // staged chunks discarded and re-requested (I2C RETRY)
    uint32_t errors;         // UF2 blocks that failed to program/validate
};

// Copies the current firmware-upgrade progress into `out`.
void fwupgrade_get_status(FwUpgradeStatus *out);

// These are the functions that upgrade over I2C
void fwupgrade_i2c_begin();

// Resets progress and marks the upgrade inactive *without* committing anything
// -- the I2C ABORT path (distinct from begin(), which starts a fresh upgrade).
void fwupgrade_i2c_abort();

// Copies `data` (a chunk just received over I2C, not yet confirmed good) into
// the staging buffer. Pure memcpy + bookkeeping, no flash access -- safe to
// call from core0's ordinary message-processing loop.
bool fwupgrade_i2c_stage_chunk(const uint8_t *data, size_t length);

// Runs the per-uf2_block split + handle_uf2_block loop over whatever is
// currently staged, committing it (erasing/programming flash as needed). A
// no-op returning true if nothing is staged. Must be called from core0.
bool fwupgrade_i2c_commit_staged();

// Discards whatever is currently staged without committing it -- used when
// the host sends RETRY (it detected a CRC/length mismatch on the chunk it
// just acked, so the client must forget it; it will be resent). Pure
// bookkeeping, no flash access.
void fwupgrade_i2c_discard_staged();

void fwupgrade_i2c_finished();