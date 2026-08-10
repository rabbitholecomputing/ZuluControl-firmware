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

// Parses main.cpp's cached I2C WebUI JSON (via webui_data.h) into a small
// fixed-size in-memory model the screens can read directly, using
// json_extract.h's minimal scanner. Re-parses on a fixed interval rather
// than a true dirty flag -- these buffers only change on I2C responses
// that arrive no faster than a few hundred ms apart in this protocol (see
// ZuluSCSI_WebUI.cpp's STATUS_INTERVAL_MS=3000), so a much shorter parse
// interval already behaves equivalently to a dirty flag in practice.

#pragma once

#include "../ZuluControl_config.h"
#include <cstdint>
#include <cstddef>

namespace zuluide::display {

enum class DeviceType
{
    Unknown,
    ZuluIDE,
    ZuluSCSI
};

// One fixed SCSI-ID slot (ZuluSCSI) or the single implicit device
// (ZuluIDE, always slot 0) -- mirrors ZuluSCSI-firmware's DeviceMap array:
// every slot 0..MAX_DEVICES-1 always exists so the "SCSI Map" screen can
// show all IDs, `present` (this iteration's stand-in for DeviceMap::Active)
// says whether the status JSON actually reported a device at that ID.
struct DeviceInfo
{
    int id = 0;
    char type[16] = "Unknown";  // ZuluSCSI: "CD-ROM"/"Removable"/"Floppy"/"MO"/"Tape"/"Zip"/"Unknown"
                                // ZuluIDE: the loaded image's media type ("cdrom"/"zip100"/
                                // "zip250"/"zip750"/"harddrive"/"removable"/"unknown"), or
                                // "ZuluIDE" when nothing is loaded -- see parseStatus().
    char image[MAX_FILE_PATH] = "";  // currently loaded image filename, "" if ejected/none
    bool ejected = true;
    bool present = false;
    // ZuluIDE only: the status JSON's "isPrimary" flag (primary vs. secondary
    // IDE device -- shown as "pri"/"sec" on the ZuluIDE main screen). Always
    // true / unused on ZuluSCSI.
    bool primary = true;
    // ZuluIDE only: the status JSON's "isDeferred" flag -- a new image load is
    // pending until the host ejects the currently-locked media. Surfaced as a
    // "[Host deferred eject]" banner. Unused on ZuluSCSI.
    bool deferred = false;
};

// One entry in the combined filename index (see DisplayData::MAX_FILENAME_INDEX):
// just enough to jump straight back to that entry's raw string bytes
// within its owning scsi id's cached filenames JSON later
// (GetIndexedFilename()) without re-scanning the array from the start.
// Which scsi id an entry belongs to isn't stored per-entry (that's what
// _scsiIdIndexStart/_scsiIdIndexEnd are for) -- GetIndexedFilename()
// re-fetches that id's GetFilenamesJson() base pointer itself, so `offset`
// is only ever interpreted relative to the correct buffer. uint16_t is
// enough headroom for both fields -- main.cpp's FILENAMES_JSON_CACHE_SIZE
// is 51200 bytes (< 65536), and no single path realistically approaches
// 65535 bytes.
struct FilenameIndexEntry
{
    uint16_t offset = 0;  // start of this entry's raw string content within its scsi id's GetFilenamesJson()
    uint16_t length = 0;  // raw byte length up to (not including) the closing quote
};

class DisplayData
{
public:
    static constexpr int MAX_DEVICES = 16;

    // Combined filename-index capacity, SHARED across all MAX_DEVICES scsi
    // ids at once (not a separate 1024 per device) -- rebuildFilenameIndex()
    // lays every device's entries out back-to-back in one flat array
    // (1024 entries x 4 bytes = 4KB total, still far less than eagerly
    // copying every filename+path string up front), with
    // _scsiIdIndexStart/_scsiIdIndexEnd (two MAX_DEVICES-length arrays)
    // marking each device's half-open [start,end) slice within it. `end`
    // for whichever device fills the array to capacity is the one-past-
    // the-end sentinel value MAX_FILENAME_INDEX itself, so start/end
    // values range over MAX_FILENAME_INDEX+1 (1025) possible values even
    // though the array itself only has 1024 slots. Sharing one combined
    // budget across every device is a real, deliberate tradeoff: if the
    // *combined* total across every id's filenames JSON exceeds this cap,
    // ids scanned later in rebuildFilenameIndex()'s id 0..MAX_DEVICES-1
    // order lose entries first.
    static constexpr int MAX_FILENAME_INDEX = 1024;
    static constexpr size_t STATUS_JSON_CACHE_SIZE = 1024;

    // Re-parses version/status if the internal throttle interval has
    // elapsed, and rebuilds the filename index (for every scsi id at once)
    // if any device's cached filenames JSON has changed since the last
    // call -- i.e. "whenever a new JSON comes over I2C", detected by
    // polling GetFilenamesJson()'s pointer per id at this same throttled
    // cadence (this codebase has no I2C-arrival callback into the display
    // module to hook directly). Safe to call every display_task tick.
    void Refresh();

    DeviceType GetDeviceType() const { return _deviceType; }
    bool SdPresent() const { return _sdPresent; }

    // Every slot 0..MAX_DEVICES-1 is always valid (never null) -- check
    // ->present to see whether the device actually exists.
    const DeviceInfo *GetDevice(int index) const;

    // Count of indexed filenames for `scsiId` (0 if out of range, or that
    // device has no cached filenames yet). The index is kept up to date
    // automatically by Refresh() -- there's no separate "ensure ready"
    // call to make first.
    int IndexedFilenameCount(int scsiId) const;

    // Entries used across every scsi id's slice of the combined index, out of
    // the MAX_FILENAME_INDEX the array holds -- the "images indexed" figure the
    // Usage screen shows. Equal to the last device's end offset, since
    // rebuildFilenameIndex() lays the slices out back-to-back from 0.
    int IndexedFilenameTotal() const { return _scsiIdIndexEnd[MAX_DEVICES - 1]; }

    // Extracts filename index entry `index` (0-based within `scsiId`'s own
    // [start,end) slice, i.e. exactly the range IndexedFilenameCount(scsiId)
    // describes) directly from that device's cached JSON via its indexed
    // offset/length -- O(entry length), not a re-scan of the array -- so
    // scrolling through the list in BrowseScreen never re-parses anything.
    // `outPath`/`outFilename` may be nullptr if that value isn't needed.
    // Returns false if `scsiId` or `index` is out of range.
    bool GetIndexedFilename(int scsiId, int index, char *outPath, size_t outPathSize,
                             char *outFilename, size_t outFilenameSize) const;

    // Stateless lookup for InfoScreen: scans `scsiId`'s cached filenames
    // list (the same lighter GetFilenamesJson()/RequestFilenames() source
    // the filename index uses -- no I2C_CLIENT_FETCH_IMAGES_JSON fetch)
    // for the entry whose basename matches `imageName`, returning the
    // directory portion (everything before the last '/', or "/" if none)
    // of its full path. Independent of the filename index above (a
    // one-off lookup, not a hot scrolling path). Returns false if not found.
    bool FindFolderForImage(int scsiId, const char *imageName, char *outFolder, size_t outFolderSize) const;

private:
    uint32_t _lastParseMs = 0;

    DeviceType _deviceType = DeviceType::Unknown;
    bool _sdPresent = false;

    // Cached snapshot of GetCurrentStatusJson(), taken once per Refresh()
    // so parsing works against a stable local copy rather than re-reading
    // main.cpp's shared buffer on every field lookup.
    char _statusJsonCache[STATUS_JSON_CACHE_SIZE] = {0};

    DeviceInfo _devices[MAX_DEVICES];

    // Combined filename index -- see MAX_FILENAME_INDEX's comment.
    FilenameIndexEntry _filenameIndex[MAX_FILENAME_INDEX];
    int _scsiIdIndexStart[MAX_DEVICES] = {0};
    int _scsiIdIndexEnd[MAX_DEVICES] = {0};
    // Last-seen GetFilenamesJson() pointer per scsi id, so Refresh() can
    // tell whether any device's cache changed without unconditionally
    // re-scanning all MAX_DEVICES devices' JSON on every single call.
    const char *_lastFilenamesJsonPtr[MAX_DEVICES] = {nullptr};

    void parseVersion();
    void parseStatus();
    void refreshFilenameIndexIfChanged();
    void rebuildFilenameIndex();

    // Sorts _filenameIndex[start, end) (one scsi id's slice) into ascending
    // order by raw path string -- see the .cpp for the stack-based 3-way
    // radix quicksort implementation and the reasoning behind its fixed,
    // log2(MAX_FILENAME_INDEX)-sized explicit stack.
    void sortFilenameIndexRange(int scsiId, int start, int end);
};

}  // namespace zuluide::display
