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

// Read-only accessors into main.cpp's cached I2C WebUI JSON buffers
// (currentStatus, filenames_json, versionJson, deviceListJson), so the new
// display/control panel (src/display/display_data.cpp) can read the same
// data the browser-facing web UI already serves, without exposing those
// buffers as raw globals or duplicating any I2C request/response handling.
// Actions (load image, eject) are issued directly via the existing
// zuluide::i2c::client::EnqueueRequest*() functions in
// ZuluControlI2CClient.h -- no new accessors needed for those.

#pragma once

// Returns the cached `{"devices":[...],"sdPresent":bool}` (ZuluSCSI) or
// `{"sdPresent":bool,"isPrimary":bool,"image":{...},...}` (ZuluIDE) status
// JSON, NUL-terminated. Empty string until the first status update arrives.
const char *GetCurrentStatusJson();

// Returns whether the SD card is present, tracked separately from
// GetCurrentStatusJson()'s raw JSON: `sdPresent` is only ever injected
// into the *browser-facing* /status.json response (main.cpp's
// fs_open_custom(), built from this same flag) -- the raw
// I2C_SERVER_SYSTEM_STATUS_JSON cached in `currentStatus` never actually
// carries that field, so parsing it out of GetCurrentStatusJson() would
// always fail. Updated from I2C_SERVER_SD_STATUS_CHANGE (ProcessSDStatus()).
bool GetSdPresent();

// Returns the cached `{"clientAPIVersion":..,"clientFWVersion":..,
// "serverAPIVersion":..,"deviceType":"ZuluSCSI"|"ZuluIDE"|"Unknown"}` JSON.
const char *GetVersionJson();

// Returns the cached ZuluSCSI-only `[{"id":N,"type":"..."}]` device list
// JSON. Empty string on ZuluIDE or before the first fetch.
const char *GetDeviceListJson();

// Returns the cached `{"filenames":["/path1",...]}` JSON for `scsiId`
// (ZuluIDE always uses slot 0), NUL-terminated. Empty string if nothing
// has been cached for that ID yet -- see RequestFilenames().
const char *GetFilenamesJson(int scsiId);

// Kicks off (or continues waiting on) an I2C_CLIENT_FETCH_FILENAMES fetch
// for `scsiId`, mirroring the /filenames CGI handler's cache-hit / fetch-
// in-progress / cache-miss logic (main.cpp's cgi_handler_filenames) so the
// display doesn't duplicate that state-machine knowledge. Returns true
// once GetFilenamesJson(scsiId) has real data ready to read; false while a
// fetch is pending (either just started, or already in flight for this or
// another scsiId -- this protocol/cache only supports one fetch at a time,
// same limitation the web UI has).
bool RequestFilenames(int scsiId);
