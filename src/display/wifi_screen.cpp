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

#include "wifi_screen.h"
#include "screen_registry.h"
#include "icons.h"
#include <pico/time.h>
#include <cstdio>

namespace zuluide::display {

namespace {

// Milliseconds between radio re-polls -- cyw43_wifi_get_rssi() does an ioctl
// down to the radio, so once a second (rather than every display tick) keeps
// the signal/connection readout live without hammering the driver.
constexpr uint32_t kPollIntervalMs = 1000;

// Data-row baselines (7px font, ~12px pitch) under the title rule at y=10.
constexpr int kRow0Y = 16;
constexpr int kRowStep = 12;

// Page-1 action bar (mirrors InfoScreen's): a ▶ marker slot (kBarLead) reserved
// before each label, kBarGap between entries.
constexpr int kBarY = 54;
constexpr int kBarLead = 10;
constexpr int kBarGap = 6;

// Minimum time "Reconnecting..." stays up after the connect is issued, so the
// last-known-good status (link not yet actually torn down) can't clear it in
// the frame or two before the blocking connect starts.
constexpr uint32_t kReconnectMinShowMs = 750;

// Wall-clock delay between pressing Reconnect and actually issuing the
// (main-loop-blocking) connect -- long enough for the "Reconnecting..." status
// page to have been drawn AND DMA'd to the panel first (framebuffer pushes are
// gated to ~33ms; see display_task.cpp's kPushIntervalMs / pollAndPush()).
constexpr uint32_t kReconnectDeferMs = 250;

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }

// A dotted-quad field is empty until the link is up; show a placeholder rather
// than a blank value on the details page.
const char *valueOrDash(const char *s) { return (s && s[0]) ? s : "---"; }

}  // namespace

void WiFiScreen::poll()
{
    GetWiFiStatus(&_status);
    _lastPollMs = millis();
}

void WiFiScreen::init(int index)
{
    Screen::init(index);
    _page = 0;
    _barSel = kBarBack;
    _reconnecting = false;
    _reconnectFireMs = 0;
    _deferredAction = nullptr;
    _deferredDueMs = 0;
    poll();
    forceDraw();
}

void WiFiScreen::deferAction(void (*fn)(WiFiScreen *self), uint32_t delayMs)
{
    _deferredAction = fn;
    _deferredDueMs = millis() + delayMs;
}

void WiFiScreen::runReconnect(WiFiScreen *self)
{
    RequestWiFiReconnect();               // stop (leave) then start (WIFIInit)
    self->_reconnectFireMs = millis();
}

void WiFiScreen::tick()
{
    uint32_t now = millis();

    // Fire a deferred button action once the frame that queued it has painted
    // and been pushed (a couple of frames later) -- e.g. the Reconnect connect,
    // which blocks the main loop and would otherwise freeze the panel on the
    // details page instead of the "Reconnecting..." status page.
    if (_deferredAction && (int32_t)(now - _deferredDueMs) >= 0)
    {
        void (*fn)(WiFiScreen *) = _deferredAction;
        _deferredAction = nullptr;
        fn(this);
    }

    if (_reconnecting)
    {
        // Poll every frame (not throttled) so the page switches to the real
        // status the instant the link is back. Don't clear until the connect
        // has actually been issued (_reconnectFireMs != 0) and it's had its
        // minimum on-screen time -- otherwise the still-up old link would clear
        // it before the reconnect even starts.
        poll();
        if (_reconnectFireMs != 0 &&
            (uint32_t)(now - _reconnectFireMs) > kReconnectMinShowMs &&
            (_status.state == WiFiStatusInfo::State::Connected ||
             _status.state == WiFiStatusInfo::State::Error))
        {
            _reconnecting = false;
        }
        forceDraw();
    }
    else if ((uint32_t)(now - _lastPollMs) >= kPollIntervalMs)
    {
        poll();
        forceDraw();
    }

    Screen::tick();
}

void WiFiScreen::draw()
{
    _fb->drawText(0, 0, "WiFi");
    _fb->drawHLine(0, 10, 112);

    if (_data->SdPresent())
        _fb->drawBitmap(115, 0, icon_sd, 12, 12);
    else
        _fb->drawBitmap(115, 0, icon_nosd, 12, 12);

    // ◀ ▶ paging hint -- the status and details pages are a scrollable pair, so
    // both pages carry it (drawn between the title and the SD icon).
    _fb->drawBitmap(93, 1, icon_select_left, 8, 8);
    _fb->drawBitmap(103, 1, icon_select, 8, 8);

    if (_page == 1)
        drawDetailsPage();
    else
        drawStatusPage();
}

void WiFiScreen::drawStatusPage()
{
    char line[72];  // wide enough for "SSID: " + a full 63-char SSID

    // A reconnect in flight overrides the (possibly stale) polled state: show
    // "Reconnecting..." where a normal fresh connect would read "Connecting...".
    if (_reconnecting)
    {
        snprintf(line, sizeof(line), "SSID: %s", _status.ssid);
        char fitted[24];
        ellipsizeToWidth(line, fitted, sizeof(fitted), Framebuffer128x64::WIDTH);
        _fb->drawText(0, kRow0Y, fitted);

        _fb->drawText(0, kRow0Y + kRowStep, "Reconnecting...");

        snprintf(line, sizeof(line), "MAC:%s", _status.mac);
        _fb->drawText(0, kRow0Y + kRowStep * 3, line);
        return;
    }

    switch (_status.state)
    {
        case WiFiStatusInfo::State::NoSSID:
        {
            // No SSID configured -- point the user at the host .ini file.
            const bool ide = _data->GetDeviceType() == DeviceType::ZuluIDE;
            printCenteredText("No SSID setup in", kRow0Y + kRowStep);
            printCenteredText(ide ? "the zuluide.ini file" : "the zuluscsi.ini file",
                              kRow0Y + kRowStep * 2);
            break;
        }

        case WiFiStatusInfo::State::Connected:
        {
            // SSID may be long -- ellipsize the whole line to the panel width.
            snprintf(line, sizeof(line), "SSID: %s", _status.ssid);
            char fitted[24];
            ellipsizeToWidth(line, fitted, sizeof(fitted), Framebuffer128x64::WIDTH);
            _fb->drawText(0, kRow0Y, fitted);

            snprintf(line, sizeof(line), "Strength: %ddBm", (int)_status.rssi);
            _fb->drawText(0, kRow0Y + kRowStep, line);

            snprintf(line, sizeof(line), "IP: %s", _status.ip);
            _fb->drawText(0, kRow0Y + kRowStep * 2, line);

            // "MAC:" + 17-char address = 21 glyphs = 126px, just fits (no space
            // after the colon so it stays within the 128px width).
            snprintf(line, sizeof(line), "MAC:%s", _status.mac);
            _fb->drawText(0, kRow0Y + kRowStep * 3, line);
            break;
        }

        case WiFiStatusInfo::State::Error:
        case WiFiStatusInfo::State::Connecting:
        {
            snprintf(line, sizeof(line), "SSID: %s", _status.ssid);
            char fitted[24];
            ellipsizeToWidth(line, fitted, sizeof(fitted), Framebuffer128x64::WIDTH);
            _fb->drawText(0, kRow0Y, fitted);

            if (_status.state == WiFiStatusInfo::State::Error)
                _fb->drawText(0, kRow0Y + kRowStep, "Connection Error:");
            else
                _fb->drawText(0, kRow0Y + kRowStep, "Connecting...");

            ellipsizeToWidth(_status.error, line, sizeof(line), Framebuffer128x64::WIDTH);
            _fb->drawText(0, kRow0Y + kRowStep * 2, line);

            snprintf(line, sizeof(line), "MAC:%s", _status.mac);
            _fb->drawText(0, kRow0Y + kRowStep * 3, line);
            break;
        }
    }
}

void WiFiScreen::drawDetailsPage()
{
    char line[40];

    snprintf(line, sizeof(line), "IP: %s", valueOrDash(_status.ip));
    _fb->drawText(0, kRow0Y, line);

    snprintf(line, sizeof(line), "NM: %s", valueOrDash(_status.netmask));
    _fb->drawText(0, kRow0Y + kRowStep, line);

    snprintf(line, sizeof(line), "GW: %s", valueOrDash(_status.gateway));
    _fb->drawText(0, kRow0Y + kRowStep * 2, line);

    drawActionBar();
}

void WiFiScreen::drawActionBar()
{
    const char *opts[kBarCount] = { "Back", "Reconnect" };

    int x = 0;
    for (int i = 0; i < kBarCount; i++)
    {
        if (_barSel == i)
            _fb->drawBitmap(x, kBarY, icon_select, 8, 8);
        int tx = x + kBarLead;
        _fb->drawText(tx, kBarY, opts[i]);
        x = tx + Framebuffer128x64::textWidth(opts[i]) + kBarGap;
    }
}

void WiFiScreen::showPage(int page)
{
    _page = page;
    if (_page == 1)
    {
        _barSel = kBarBack;  // ▶ defaults to Back each time the page opens
        poll();              // refresh IP/NM/GW as the details page comes up
    }
    forceDraw();
}

void WiFiScreen::activateBar()
{
    if (_barSel == kBarReconnect)
    {
        // Switch to the status page immediately and show "Reconnecting...", but
        // defer the actual (blocking) connect a couple of frames so that page
        // paints/pushes first. See runReconnect()/tick().
        _reconnecting = true;
        _reconnectFireMs = 0;
        deferAction(&WiFiScreen::runReconnect, kReconnectDeferMs);
    }
    // Both actions return to the status page; Reconnect additionally kicks off
    // the deferred connection restart above.
    showPage(0);
}

void WiFiScreen::back()
{
    ChangeScreen(DisplayScreenType::Settings, getOriginalIndex());
}

void WiFiScreen::rotaryChange(int direction)
{
    if (_page == 0)
    {
        // Any scroll on the status page drops to the network-details page.
        showPage(1);
        return;
    }

    // Page 1: move the ▶ marker along the action bar (wraps).
    if (direction > 0)
        _barSel = (_barSel + 1) % kBarCount;
    else if (direction < 0)
        _barSel = (_barSel - 1 + kBarCount) % kBarCount;
    forceDraw();
}

void WiFiScreen::shortRotaryPress()
{
    if (_page == 1)
        activateBar();
    else
        back();
}

void WiFiScreen::shortUserPress()
{
    if (_page == 1)
        showPage(0);
    else
        back();
}

void WiFiScreen::shortEjectPress()
{
    if (_page == 1)
        showPage(0);
    else
        back();
}

void WiFiScreen::longRotaryPress() { back(); }
void WiFiScreen::longUserPress() { back(); }
void WiFiScreen::longEjectPress() { back(); }

}  // namespace zuluide::display
