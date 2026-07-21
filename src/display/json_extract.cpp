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

#include "json_extract.h"
#include <cstring>
#include <cstdlib>

namespace zuluide::display::json {

namespace {

size_t skipWhitespace(const char *json, size_t i, size_t len)
{
    while (i < len && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r'))
        i++;
    return i;
}

// Naive substring search for `"key":` within json[from..len).
bool findKeyPattern(const char *json, size_t len, const char *key, size_t from, size_t *matchEnd)
{
    size_t keyLen = strlen(key);
    // pattern = "key":
    for (size_t i = from; i + keyLen + 3 <= len; i++)
    {
        if (json[i] != '"')
            continue;
        if (memcmp(json + i + 1, key, keyLen) != 0)
            continue;
        if (json[i + 1 + keyLen] != '"')
            continue;
        size_t afterQuote = skipWhitespace(json, i + 2 + keyLen, len);
        if (afterQuote >= len || json[afterQuote] != ':')
            continue;
        *matchEnd = afterQuote + 1;
        return true;
    }
    return false;
}

bool locateValue(const char *json, size_t len, const char *key, size_t *valuePos)
{
    size_t afterColon;
    if (!findKeyPattern(json, len, key, 0, &afterColon))
        return false;
    *valuePos = skipWhitespace(json, afterColon, len);
    return *valuePos < len;
}

// `start` must point at an opening bracket (`{` or `[`). Scans forward,
// tracking nesting depth and string state (so brackets inside quoted
// strings don't confuse it), and returns the index of the matching closing
// bracket (inclusive) via *endInclusive.
bool scanBalanced(const char *json, size_t len, size_t start, char open, char close, size_t *endInclusive)
{
    int depth = 0;
    bool inString = false;
    for (size_t i = start; i < len; i++)
    {
        char c = json[i];
        if (inString)
        {
            if (c == '\\')
                i++;  // skip escaped char
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
        {
            inString = true;
        }
        else if (c == open)
        {
            depth++;
        }
        else if (c == close)
        {
            depth--;
            if (depth == 0)
            {
                *endInclusive = i;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

bool FindString(const char *json, size_t len, const char *key, char *outBuf, size_t outBufSize)
{
    size_t pos;
    if (!locateValue(json, len, key, &pos) || outBufSize == 0)
        return false;
    if (json[pos] != '"')
        return false;
    pos++;

    size_t outIdx = 0;
    while (pos < len && json[pos] != '"')
    {
        char c = json[pos];
        if (c == '\\' && pos + 1 < len)
        {
            pos++;
            c = json[pos];
        }
        if (outIdx + 1 < outBufSize)
            outBuf[outIdx++] = c;
        pos++;
    }
    outBuf[outIdx] = '\0';
    return true;
}

bool FindInt(const char *json, size_t len, const char *key, int *out)
{
    size_t pos;
    if (!locateValue(json, len, key, &pos))
        return false;
    if (json[pos] != '-' && (json[pos] < '0' || json[pos] > '9'))
        return false;
    *out = atoi(json + pos);
    return true;
}

bool FindUInt64(const char *json, size_t len, const char *key, uint64_t *out)
{
    size_t pos;
    if (!locateValue(json, len, key, &pos))
        return false;
    if (json[pos] < '0' || json[pos] > '9')
        return false;
    *out = strtoull(json + pos, nullptr, 10);
    return true;
}

bool FindBool(const char *json, size_t len, const char *key, bool *out)
{
    size_t pos;
    if (!locateValue(json, len, key, &pos))
        return false;
    if (pos + 4 <= len && memcmp(json + pos, "true", 4) == 0)
    {
        *out = true;
        return true;
    }
    if (pos + 5 <= len && memcmp(json + pos, "false", 5) == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

bool FindArray(const char *json, size_t len, const char *key, size_t *arrStart, size_t *arrLen)
{
    size_t pos;
    if (!locateValue(json, len, key, &pos) || json[pos] != '[')
        return false;
    size_t endIncl;
    if (!scanBalanced(json, len, pos, '[', ']', &endIncl))
        return false;
    *arrStart = pos;
    *arrLen = endIncl - pos + 1;
    return true;
}

bool FindObject(const char *json, size_t len, const char *key, size_t *objStart, size_t *objLen)
{
    size_t pos;
    if (!locateValue(json, len, key, &pos) || json[pos] != '{')
        return false;
    size_t endIncl;
    if (!scanBalanced(json, len, pos, '{', '}', &endIncl))
        return false;
    *objStart = pos;
    *objLen = endIncl - pos + 1;
    return true;
}

bool NextArrayElement(const char *json, size_t len, size_t *cursor, size_t *elemStart, size_t *elemLen)
{
    if (len == 0 || json[0] != '[')
        return false;

    size_t pos = (*cursor == 0) ? 1 : *cursor;
    while (pos < len && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
                         json[pos] == '\r' || json[pos] == ','))
        pos++;

    if (pos >= len || json[pos] != '{')
        return false;  // ']' or end of buffer -- no more elements

    size_t endIncl;
    if (!scanBalanced(json, len, pos, '{', '}', &endIncl))
        return false;

    *elemStart = pos;
    *elemLen = endIncl - pos + 1;
    *cursor = endIncl + 1;
    return true;
}

bool NextStringElementSpan(const char *json, size_t len, size_t *cursor, size_t *strStart, size_t *strLen)
{
    if (len == 0 || json[0] != '[')
        return false;

    size_t pos = (*cursor == 0) ? 1 : *cursor;
    while (pos < len && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
                         json[pos] == '\r' || json[pos] == ','))
        pos++;

    if (pos >= len || json[pos] != '"')
        return false;  // ']' or end of buffer -- no more elements
    pos++;
    size_t start = pos;

    while (pos < len && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < len)
            pos++;
        pos++;
    }

    *strStart = start;
    *strLen = pos - start;
    if (pos < len)
        pos++;  // skip closing quote

    *cursor = pos;
    return true;
}

bool NextStringElement(const char *json, size_t len, size_t *cursor, char *outBuf, size_t outBufSize)
{
    size_t strStart, strLen;
    if (!NextStringElementSpan(json, len, cursor, &strStart, &strLen))
        return false;

    size_t outIdx = 0;
    size_t end = strStart + strLen;
    for (size_t pos = strStart; pos < end; pos++)
    {
        char c = json[pos];
        if (c == '\\' && pos + 1 < end)
        {
            pos++;
            c = json[pos];
        }
        if (outBufSize > 0 && outIdx + 1 < outBufSize)
            outBuf[outIdx++] = c;
    }
    if (outBufSize > 0)
        outBuf[outIdx] = '\0';

    return true;
}

}  // namespace zuluide::display::json
