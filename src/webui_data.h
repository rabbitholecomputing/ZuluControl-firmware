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

#include <cstddef>
#include <cstdint>

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

// True from the first I2C_SERVER_SYSTEM_STATUS_JSON push onward, false
// before it. Distinguishes "haven't heard from the device yet" from "device
// reported an empty status" -- GetCurrentStatusJson() alone can't tell
// those apart since it starts out as an empty string either way. Used to
// hold the boot splash screen up until the first real status arrives (see
// display_task.cpp's tickBootSequence()).
bool HasReceivedStatus();

// Returns the cached `{"clientAPIVersion":..,"clientFWVersion":..,
// "serverAPIVersion":..,"deviceType":"ZuluSCSI"|"ZuluIDE"|"Unknown"}` JSON.
const char *GetVersionJson();

// True once the host's I2C API version has been received and its major
// version doesn't match this client's I2C_API_VERSION (or the host sent no
// version at all) -- see ProcessServerAPIVersion()'s matching_major_version
// for the actual comparison. The boot splash screen shows this instead of
// the normal "I2C API vX.X.X" line when true (see tickBootSequence()).
bool IsApiVersionMismatch();

// Just the version portion of the host's reported API version (e.g.
// "4.0.0" out of a raw "4.0.0 ZuluSCSI" response), NUL-terminated. Empty
// string before the exchange happens or if the host sent no version.
const char *GetServerAPIVersionOnly();

// Returns the cached ZuluSCSI-only `[{"id":N,"type":"..."}]` device list
// JSON. Empty string on ZuluIDE or before the first fetch.
const char *GetDeviceListJson();

// Returns the cached `{"filenames":["/path1",...]}` JSON for `scsiId`
// (ZuluIDE always uses slot 0), NUL-terminated. Empty string if nothing
// has been cached for that ID yet -- see RequestFilenames().
const char *GetFilenamesJson(int scsiId);

// Raw I2C payload bytes received so far for the filenames fetch currently
// (or most recently) in progress, reset to 0 each time a new fetch starts.
// Lets a "Loading" screen show fetch progress while RequestFilenames()
// below is still returning false.
size_t GetFilenamesBytesReceived();

// Bytes of the single shared filenames cache buffer currently occupied by the
// cached JSON of every scsi id together (ZuluIDE only ever fills slot 0) --
// i.e. how much of FILENAMES_JSON_CACHE_SIZE is in use. Unlike
// GetFilenamesBytesReceived() above (raw payload of the latest fetch alone),
// this is cumulative across ids and counts the JSON framing written around the
// payload. Backs the Usage screen's cache readout.
size_t GetFilenamesCacheBytesUsed();

// True while a filenames fetch is actually in flight (server has
// acknowledged/started sending, i.e. ProcessUpdateFilenames() through the
// closing sentinel in ProcessFilename()). Unlike RequestFilenames(), this
// is a pure read with no side effects (it never kicks off a fetch), and it
// isn't scoped to one scsiId -- the underlying cache only ever has one
// fetch in flight at a time (see RequestFilenames()'s comment), so this is
// meaningful from any screen, not just the one that requested it. The
// display task uses this to overlay a "File list Loading" popup on
// whichever screen is active, since the server can also push a filenames
// update unprompted (e.g. right after boot) with no screen having called
// RequestFilenames() at all.
bool IsFilenamesFetchActive();

// Kicks off (or continues waiting on) an I2C_CLIENT_FETCH_FILENAMES fetch
// for `scsiId`, mirroring the /filenames CGI handler's cache-hit / fetch-
// in-progress / cache-miss logic (main.cpp's cgi_handler_filenames) so the
// display doesn't duplicate that state-machine knowledge. Returns true
// once GetFilenamesJson(scsiId) has real data ready to read; false while a
// fetch is pending (either just started, or already in flight for this or
// another scsiId -- this protocol/cache only supports one fetch at a time,
// same limitation the web UI has).
bool RequestFilenames(int scsiId);

// ── WiFi status (the control board's own RM2 radio) ───────────────────────────

// A snapshot of the control board's WiFi link to the WebUI network. The SSID
// (and password) arrive from the host over I2C (I2C_SERVER_SSID), but the
// radio, IP, MAC and signal strength are all local to this board's RM2 -- so
// reading any of this needs no new I2C request. Filled by GetWiFiStatus().
struct WiFiStatusInfo
{
    enum class State
    {
        NoSSID,      // no SSID configured (none from the host, none compiled in)
        Connecting,  // SSID known, link not up yet (joining / waiting for IP)
        Connected,   // associated and holding an IP address
        Error        // SSID known, association failed (bad auth / no network / ...)
    };

    State state = State::NoSSID;
    char ssid[64] = "";      // configured/associated SSID (valid unless NoSSID)
    char ip[32] = "";        // dotted IPv4, valid when Connected
    char netmask[32] = "";   // dotted IPv4 netmask, valid when Connected
    char gateway[32] = "";   // dotted IPv4 gateway, valid when Connected
    char mac[18] = "";       // "AA:BB:CC:DD:EE:FF", valid once the radio is up
    int32_t rssi = 0;        // signal strength in dBm, valid when Connected
    const char *error = "";  // human-readable reason, valid when Error
};

// Fills `out` with the current state of the control board's RM2 radio: the SSID
// the host configured, plus the live cyw43 link status / RSSI / MAC / IP that
// main()'s WiFi state machine already maintains. Read-only -- issues no I2C
// traffic. Safe to call any time after cyw43 init (before that it reports
// NoSSID with an empty MAC).
void GetWiFiStatus(WiFiStatusInfo *out);

// Kicks the control board's WiFi state machine back to (re)initialisation, so
// it tears the current association down and connects again -- the same entry
// point the host's I2C_SERVER_WIFI_CONNECT already uses. Safe to call from the
// display (it and the state machine both run on core0's main loop). Backs the
// WiFi status page's "Reconnect" action.
void RequestWiFiReconnect();
