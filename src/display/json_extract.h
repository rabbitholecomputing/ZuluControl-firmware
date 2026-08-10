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

// Minimal, dependency-free field-extraction scanner -- not a general JSON
// parser -- tailored to the small set of fixed, flat JSON shapes this I2C
// WebUI protocol already produces (see ZuluSCSI-firmware's
// lib/ZuluSCSI_WebUI_RP2MCU/ZuluSCSI_WebUI.cpp's sprintf()-built JSON and
// this project's main.cpp caches): `{"key":"str","key2":123,"key3":true}`,
// arrays of such objects, and one level of nesting (ZuluIDE's `"image":{...}`).
// No unicode escapes, no deep nesting, no allocation -- matches this
// codebase's existing bare-metal, no-new-dependency style.

#pragma once

#include <cstdint>
#include <cstddef>

namespace zuluide::display::json {

// Looks for `"key":"value"` within json[0..len) and copies `value`
// (unescaped only for the `\"` and `\\` sequences this protocol could
// plausibly emit from filesystem paths) into outBuf, NUL-terminated,
// truncated to fit outBufSize. Returns false if the key isn't found.
bool FindString(const char *json, size_t len, const char *key, char *outBuf, size_t outBufSize);

// Looks for `"key":<integer>` and parses it as a signed/unsigned integer.
bool FindInt(const char *json, size_t len, const char *key, int *out);
bool FindUInt64(const char *json, size_t len, const char *key, uint64_t *out);

// Looks for `"key":true` or `"key":false`.
bool FindBool(const char *json, size_t len, const char *key, bool *out);

// Finds the array value of `key` (e.g. `"devices":[...]`), returning its
// span (`*arrStart`/`*arrLen`), INCLUDING the surrounding `[`/`]`, as an
// offset/length into `json` -- pass that span straight to NextArrayElement.
bool FindArray(const char *json, size_t len, const char *key, size_t *arrStart, size_t *arrLen);

// Finds the object value of `key` (e.g. ZuluIDE's `"image":{...}`),
// returning its span INCLUDING the surrounding `{`/`}` so FindString/
// FindInt/etc. can be applied directly to json+objStart.
bool FindObject(const char *json, size_t len, const char *key, size_t *objStart, size_t *objLen);

// Iterates a top-level JSON array's elements (`json[0..len)` must be a
// span starting with `[` and ending with the matching `]`, e.g. from
// FindArray, or the whole buffer for a bare top-level array like
// `imageJson`/`deviceListJson`). `*cursor` is an opaque progress marker;
// initialize it to 0 before the first call. Each call advances it past one
// element and returns that element's span (`*elemStart`/`*elemLen`,
// INCLUDING its `{`/`}`) via out-params. Returns false once there are no
// more elements (cursor is left in an undefined-but-safe-to-ignore state).
bool NextArrayElement(const char *json, size_t len, size_t *cursor, size_t *elemStart, size_t *elemLen);

// Same iteration contract as NextArrayElement, but for arrays of plain
// JSON strings (e.g. `{"filenames":["/a.iso","/b.iso"]}`'s array) rather
// than objects: copies each element's unescaped string content into
// outBuf (NUL-terminated, truncated to fit) instead of returning a span.
bool NextStringElement(const char *json, size_t len, size_t *cursor, char *outBuf, size_t outBufSize);

// Same iteration as NextStringElement, but returns the element's raw
// (still-escaped, if any) span (`*strStart`/`*strLen`, relative to `json`,
// EXCLUDING the surrounding quotes) instead of copying/unescaping it into
// a buffer. Lets a caller build a lightweight offset/length index over a
// large array of strings up front (a single pass) without allocating or
// copying every element -- then jump straight to any one element later by
// re-reading `json[*strStart .. *strStart+*strLen)` directly, no re-scan.
// Safe for this protocol's filenames (plain filesystem paths, never
// actually escaped in practice, matching FindString's existing assumption).
bool NextStringElementSpan(const char *json, size_t len, size_t *cursor, size_t *strStart, size_t *strLen);

}  // namespace zuluide::display::json
