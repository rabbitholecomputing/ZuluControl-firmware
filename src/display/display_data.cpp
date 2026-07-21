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

#include "display_data.h"
#include "json_extract.h"
#include "../webui_data.h"
#include <pico/time.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <memory>

namespace zuluide::display {

namespace {
constexpr uint32_t kParseIntervalMs = 200;

// Matches a filenames-cache basename against the status JSON's "image"
// field case-insensitively, treating `imageName` (after trimming any
// trailing whitespace -- SCSI status fields are conventionally
// space-padded) as a PREFIX of `baseName` rather than requiring exact
// equality. Confirmed necessary, not just defensive: the status JSON's
// "image" value is itself sometimes truncated at the source (a separate,
// more size-constrained field on the connected device than the full
// per-file directory listing this filenames cache comes from) -- verified
// by the fact InfoScreen's own scrolling marquee, which draws `imageName`
// directly, stops scrolling into new characters at the same truncation
// point, so the full name genuinely never reaches this client. A prefix
// match still recovers the folder in that case; it's naturally still an
// exact match whenever imageName isn't truncated (a prefix of equal length
// is equality). Ambiguous only if two files in the same scsi id's listing
// share an identical prefix at least as long as the (possibly-truncated)
// status value -- accepted as the best available answer given a
// truncated data source we can't widen from this side.
bool imageNameMatches(const char *baseName, const char *imageName)
{
    size_t imageLen = strlen(imageName);
    while (imageLen > 0 && isspace((unsigned char)imageName[imageLen - 1]))
        imageLen--;
    if (imageLen == 0)
        return false;

    if (strlen(baseName) < imageLen)
        return false;

    for (size_t i = 0; i < imageLen; i++)
    {
        if (tolower((unsigned char)baseName[i]) != tolower((unsigned char)imageName[i]))
            return false;
    }
    return true;
}

// Smallest N such that 2^N >= n -- used to size sortFilenameIndexRange()'s
// explicit stack from MAX_FILENAME_INDEX at compile time.
constexpr int ceilLog2(int n)
{
    int bits = 0;
    for (int v = 1; v < n; v <<= 1)
        bits++;
    return bits;
}

// Ranges at or below this size are finished with a plain insertion sort
// instead of ever being partitioned/pushed further -- standard hybrid-
// quicksort practice, and also what keeps sortFilenameIndexRange()'s fixed
// stack from ever needing to hold a partition this small.
constexpr int kInsertionSortCutoff = 12;
}

const DeviceInfo *DisplayData::GetDevice(int index) const
{
    if (index < 0 || index >= MAX_DEVICES)
        return nullptr;
    return &_devices[index];
}

void DisplayData::parseVersion()
{
    const char *json = GetVersionJson();
    size_t len = strlen(json);

    char deviceTypeStr[16] = "";
    json::FindString(json, len, "deviceType", deviceTypeStr, sizeof(deviceTypeStr));

    if (strcmp(deviceTypeStr, "ZuluSCSI") == 0)
        _deviceType = DeviceType::ZuluSCSI;
    else if (strcmp(deviceTypeStr, "ZuluIDE") == 0)
        _deviceType = DeviceType::ZuluIDE;
    else
        _deviceType = DeviceType::Unknown;
}

void DisplayData::parseStatus()
{
    const char *json = _statusJsonCache;
    size_t len = strlen(json);

    _sdPresent = GetSdPresent();

    for (int i = 0; i < MAX_DEVICES; i++)
    {
        _devices[i] = DeviceInfo{};
        _devices[i].id = i;
    }

    if (_deviceType == DeviceType::ZuluSCSI)
    {
        size_t arrStart, arrLen;
        if (!json::FindArray(json, len, "devices", &arrStart, &arrLen))
            return;

        size_t cursor = 0, elemStart, elemLen;
        const char *arr = json + arrStart;
        while (json::NextArrayElement(arr, arrLen, &cursor, &elemStart, &elemLen))
        {
            const char *elem = arr + elemStart;

            int id = -1;
            json::FindInt(elem, elemLen, "id", &id);
            if (id < 0 || id >= MAX_DEVICES)
                continue;

            DeviceInfo &dev = _devices[id];
            dev.id = id;
            dev.present = true;
            json::FindString(elem, elemLen, "type", dev.type, sizeof(dev.type));
            json::FindString(elem, elemLen, "image", dev.image, sizeof(dev.image));
            bool ejected = true;
            json::FindBool(elem, elemLen, "ejected", &ejected);
            dev.ejected = ejected;
        }
    }
    else if (_deviceType == DeviceType::ZuluIDE)
    {
        DeviceInfo &dev = _devices[0];
        dev.id = 0;
        dev.present = true;
        strncpy(dev.type, "ZuluIDE", sizeof(dev.type) - 1);

        size_t objStart, objLen;
        if (json::FindObject(json, len, "image", &objStart, &objLen))
            json::FindString(json + objStart, objLen, "filename", dev.image, sizeof(dev.image));

        dev.ejected = (dev.image[0] == '\0');
    }
}

int DisplayData::IndexedFilenameCount(int scsiId) const
{
    if (scsiId < 0 || scsiId >= MAX_DEVICES)
        return 0;
    return _scsiIdIndexEnd[scsiId] - _scsiIdIndexStart[scsiId];
}

void DisplayData::rebuildFilenameIndex()
{
    // Lays every device's entries out back-to-back in one flat array --
    // see MAX_FILENAME_INDEX's comment for why this is a single shared
    // budget rather than a per-device one.
    int count = 0;
    for (int id = 0; id < MAX_DEVICES; id++)
    {
        int start = count;
        _scsiIdIndexStart[id] = start;

        const char *json = GetFilenamesJson(id);
        size_t len = strlen(json);
        size_t arrStart, arrLen;
        if (len > 0 && json::FindArray(json, len, "filenames", &arrStart, &arrLen))
        {
            const char *arr = json + arrStart;
            size_t cursor = 0, strStart, strLen;
            while (count < MAX_FILENAME_INDEX &&
                   json::NextStringElementSpan(arr, arrLen, &cursor, &strStart, &strLen))
            {
                FilenameIndexEntry &entry = _filenameIndex[count];
                entry.offset = (uint16_t)(arrStart + strStart);
                entry.length = (uint16_t)strLen;
                count++;
            }
        }

        _scsiIdIndexEnd[id] = count;

        // Sorted here (raw JSON/SD-directory order otherwise) so
        // BrowseScreen's list is alphabetical.
        sortFilenameIndexRange(id, start, count);
    }
}

void DisplayData::sortFilenameIndexRange(int scsiId, int start, int end)
{
    if (end - start <= 1)
        return;

    const char *json = GetFilenamesJson(scsiId);

    // Byte at `depth` within entry e's raw path string, lowercased for a
    // case-insensitive sort order (the underlying entry -- offset/length
    // into the raw JSON -- is untouched, only this comparison view is
    // case-folded), or -1 once `depth` runs past its length -- the usual
    // multikey-quicksort sentinel (Bentley & Sedgewick, "Fast Algorithms
    // for Sorting and Searching Strings"), so a string that's a prefix of
    // another sorts before it.
    auto charAt = [json](const FilenameIndexEntry &e, int depth) -> int
    {
        return depth < (int)e.length ? tolower((unsigned char)json[e.offset + depth]) : -1;
    };

    // Full remaining-string comparison (like strcasecmp, but starting at
    // `depth` instead of byte 0) -- only used by the insertion-sort base
    // case below, where a handful of full comparisons is cheaper than the
    // bookkeeping needed to reuse partial work from the caller.
    auto compareFrom = [json](const FilenameIndexEntry &a, const FilenameIndexEntry &b, int depth) -> int
    {
        for (int d = depth; ; d++)
        {
            int ca = d < (int)a.length ? tolower((unsigned char)json[a.offset + d]) : -1;
            int cb = d < (int)b.length ? tolower((unsigned char)json[b.offset + d]) : -1;
            if (ca != cb)
                return ca - cb;
            if (ca == -1)
                return 0;
        }
    };

    auto swapEntries = [this](int i, int j)
    {
        FilenameIndexEntry tmp = _filenameIndex[i];
        _filenameIndex[i] = _filenameIndex[j];
        _filenameIndex[j] = tmp;
    };

    auto insertionSort = [&](int lo, int hi, int depth)
    {
        for (int i = lo + 1; i <= hi; i++)
        {
            FilenameIndexEntry key = _filenameIndex[i];
            int j = i - 1;
            while (j >= lo && compareFrom(key, _filenameIndex[j], depth) < 0)
            {
                _filenameIndex[j + 1] = _filenameIndex[j];
                j--;
            }
            _filenameIndex[j + 1] = key;
        }
    };

    // Explicit stack of pending (lo, hi, depth) jobs, in place of language-
    // level recursion (three-way, so a naive recursive version would need
    // up to 3 nested calls per partition -- an explicit stack lets us
    // choose, and bound, exactly what gets deferred). Sized to
    // ceil(log2(MAX_FILENAME_INDEX)): at every partition step below, we
    // continue the loop in place with whichever of the three partitions
    // (less-than / equal-at-depth+1 / greater-than) is LARGEST by element
    // count, and push the other two. The larger of any two pushed jobs can
    // therefore never exceed half the size of the job it split from --
    // if it did, it would have been chosen as the one to continue with
    // instead -- which is exactly the halving argument that lets ordinary
    // quicksort get away with a log2(n)-deep stack. Ranges at or below
    // kInsertionSortCutoff are finished in place without ever being
    // pushed, which is what keeps that bound meaningful in practice for
    // ordinary filenames (a string of many small, similarly-sized pushes
    // deep enough to threaten it would need deliberately adversarial
    // input, not real directory listings). `sp` is still bounds-checked on
    // every push regardless, in pushOrFinish() below, so a pathological
    // input degrades to an immediate insertion sort of the overflow
    // instead of corrupting memory.
    struct Job { int lo, hi, depth; };
    constexpr int kStackSize = ceilLog2(MAX_FILENAME_INDEX);
    Job stack[kStackSize];
    int sp = 0;

    auto pushOrFinish = [&](int lo, int hi, int depth)
    {
        if (hi <= lo)
            return;
        if (hi - lo + 1 <= kInsertionSortCutoff || sp >= kStackSize)
        {
            insertionSort(lo, hi, depth);
            return;
        }
        stack[sp++] = Job{lo, hi, depth};
    };

    int lo = start, hi = end - 1, depth = 0;
    while (true)
    {
        while (hi - lo + 1 > kInsertionSortCutoff)
        {
            // Dutch-flag 3-way partition on charAt(*, depth), pivoting on
            // the range's first element (Bentley-Sedgewick's base scheme).
            int pivot = charAt(_filenameIndex[lo], depth);
            int lt = lo, i = lo + 1, gt = hi;
            while (i <= gt)
            {
                int c = charAt(_filenameIndex[i], depth);
                if (c < pivot)
                    swapEntries(lt++, i++);
                else if (c > pivot)
                    swapEntries(i, gt--);
                else
                    i++;
            }
            // [lo,lt-1] < pivot, [lt,gt] == pivot, [gt+1,hi] > pivot.

            // pivot == -1 means every entry in [lt,gt] has already been
            // fully consumed (they're identical strings, or exact prefixes
            // ending here) -- nothing more distinguishes them, so unlike
            // Bentley-Sedgewick's plain recursive version we don't even
            // push a job for it (pushing one at the *same* depth would
            // partition on -1 again and never terminate).
            bool eqDone = (pivot == -1);
            int szLt = lt - lo;
            int szGt = hi - gt;
            int szEq = eqDone ? 0 : (gt - lt + 1);

            if (szLt >= szEq && szLt >= szGt)
            {
                if (!eqDone) pushOrFinish(lt, gt, depth + 1);
                pushOrFinish(gt + 1, hi, depth);
                hi = lt - 1;
            }
            else if (szGt >= szEq)
            {
                pushOrFinish(lo, lt - 1, depth);
                if (!eqDone) pushOrFinish(lt, gt, depth + 1);
                lo = gt + 1;
            }
            else
            {
                pushOrFinish(lo, lt - 1, depth);
                pushOrFinish(gt + 1, hi, depth);
                lo = lt;
                hi = gt;
                depth = depth + 1;
            }
        }
        insertionSort(lo, hi, depth);

        if (sp == 0)
            break;
        sp--;
        lo = stack[sp].lo;
        hi = stack[sp].hi;
        depth = stack[sp].depth;
    }
}

void DisplayData::refreshFilenameIndexIfChanged()
{
    // Polled here (rather than pushed from an I2C-arrival callback -- this
    // codebase has none into the display module) at Refresh()'s already-
    // throttled cadence: cheap enough (MAX_DEVICES pointer reads) to do
    // every call, and only triggers the actual O(total filenames) rebuild
    // when something's cache pointer has actually moved.
    const char *current[MAX_DEVICES];
    bool changed = false;
    for (int id = 0; id < MAX_DEVICES; id++)
    {
        current[id] = GetFilenamesJson(id);
        if (current[id] != _lastFilenamesJsonPtr[id])
            changed = true;
    }
    if (!changed)
        return;

    for (int id = 0; id < MAX_DEVICES; id++)
        _lastFilenamesJsonPtr[id] = current[id];

    rebuildFilenameIndex();
}

bool DisplayData::GetIndexedFilename(int scsiId, int index, char *outPath, size_t outPathSize,
                                      char *outFilename, size_t outFilenameSize) const
{
    if (scsiId < 0 || scsiId >= MAX_DEVICES)
        return false;

    int start = _scsiIdIndexStart[scsiId];
    int end = _scsiIdIndexEnd[scsiId];
    int globalIndex = start + index;
    if (index < 0 || globalIndex >= end)
        return false;

    const FilenameIndexEntry &entry = _filenameIndex[globalIndex];
    // Re-fetched fresh at read time (not cached at index-build time) so
    // `offset` is always interpreted relative to the correct buffer even
    // if this same scsiId's cache was re-fetched again since the index
    // was last rebuilt (refreshFilenameIndexIfChanged() would then rebuild
    // on the next Refresh() anyway, but this keeps a stale read between
    // those two points from reading garbage instead of just "not yet
    // updated").
    const char *json = GetFilenamesJson(scsiId);
    const char *pathStart = json + entry.offset;

    if (outPath && outPathSize > 0)
    {
        size_t n = entry.length < outPathSize - 1 ? entry.length : outPathSize - 1;
        memcpy(outPath, pathStart, n);
        outPath[n] = '\0';
    }

    if (outFilename && outFilenameSize > 0)
    {
        // Last '/' within just this entry's span -- same substring-out
        // technique as the "Folder:"/filename split elsewhere, just
        // applied directly to the indexed span instead of a full copy.
        const char *base = nullptr;
        for (size_t i = 0; i < entry.length; i++)
        {
            if (pathStart[i] == '/')
                base = pathStart + i + 1;
        }
        const char *nameStart = base ? base : pathStart;
        size_t nameLen = entry.length - (size_t)(nameStart - pathStart);

        size_t n = nameLen < outFilenameSize - 1 ? nameLen : outFilenameSize - 1;
        memcpy(outFilename, nameStart, n);
        outFilename[n] = '\0';
    }

    return true;
}

bool DisplayData::FindFolderForImage(int scsiId, const char *imageName, char *outFolder, size_t outFolderSize) const
{
    if (imageName[0] == '\0')
        return false;

    const char *json = GetFilenamesJson(scsiId);
    size_t len = strlen(json);
    if (len == 0)
        return false;

    size_t arrStart, arrLen;
    if (!json::FindArray(json, len, "filenames", &arrStart, &arrLen))
        return false;

    // Heap-allocated scratch buffer -- see RefreshBrowseList()'s pathBuf comment.
    auto pathBuf = std::make_unique<char[]>(MAX_FILE_PATH);

    const char *arr = json + arrStart;
    size_t cursor = 0;
    while (json::NextStringElement(arr, arrLen, &cursor, pathBuf.get(), MAX_FILE_PATH))
    {
        char *base = strrchr(pathBuf.get(), '/');
        const char *baseName = base ? base + 1 : pathBuf.get();
        if (!imageNameMatches(baseName, imageName))
            continue;

        if (outFolder)
        {
            if (base && base != pathBuf.get())
                snprintf(outFolder, outFolderSize, "%.*s", (int)(base - pathBuf.get()), pathBuf.get());
            else
                snprintf(outFolder, outFolderSize, "/");
        }
        return true;
    }
    return false;
}

void DisplayData::Refresh()
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (_lastParseMs != 0 && (now - _lastParseMs) < kParseIntervalMs)
        return;
    _lastParseMs = now;

    parseVersion();

    strncpy(_statusJsonCache, GetCurrentStatusJson(), sizeof(_statusJsonCache) - 1);
    _statusJsonCache[sizeof(_statusJsonCache) - 1] = '\0';
    parseStatus();

    refreshFilenameIndexIfChanged();
}

}  // namespace zuluide::display
