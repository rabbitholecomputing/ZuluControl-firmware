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

// Read-only WiFi status page, reached from the Settings screen's "WiFi" row
// (both the ZuluSCSI and ZuluIDE control-board UIs). It shows the state of the
// control board's own RM2 radio -- the same connection main()'s WiFi state
// machine drives -- via webui_data.h's GetWiFiStatus(), which needs no new I2C
// traffic (the SSID comes from the host, everything else is local to the
// radio). Three variants, matching WiFiStatusInfo::State:
//
//   Connected:  SSID / Strength (dBm) / IP / MAC
//   No SSID:    "No SSID setup in / the zulu{scsi,ide}.ini file"
//   Error:      SSID / "Connection Error:" / <reason> / MAC
//               (a still-connecting link uses the same layout with a
//                "Connecting..." header instead)
//
// Two pages:
//   Page 0 (status): the SSID / signal / IP / MAC (or no-SSID / error) view.
//     A rotary scroll (either direction) drops to page 1; any button returns to
//     the Settings screen.
//   Page 1 (network details): IP / netmask / gateway, plus a bottom action bar
//     ("Back" / "Reconnect") with a ▶ marker that defaults to Back. Rotary
//     scroll moves the marker (wraps), the rotary push activates it: Back
//     returns to page 0, Reconnect returns to page 0 *and* restarts the WiFi
//     connection (RequestWiFiReconnect()). The user/eject buttons back out to
//     page 0.
//
// tick() re-polls the radio on a throttle so the signal strength / connection
// state stay live without hammering the cyw43 driver every frame.

#pragma once

#include "screen.h"
#include "display_data.h"
#include "../webui_data.h"
#include <cstdint>

namespace zuluide::display {

class WiFiScreen : public Screen
{
public:
    WiFiScreen(Framebuffer128x64 *fb, DisplayData *data) : Screen(fb), _data(data) {}

    DisplayScreenType screenType() const override { return DisplayScreenType::WiFi; }

    void init(int index) override;
    void tick() override;
    void draw() override;

    void shortRotaryPress() override;
    void shortUserPress() override;
    void shortEjectPress() override;
    void longRotaryPress() override;
    void longUserPress() override;
    void longEjectPress() override;
    void rotaryChange(int direction) override;

private:
    DisplayData *_data;
    WiFiStatusInfo _status;
    uint32_t _lastPollMs = 0;

    // 0 = status page, 1 = network-details page (IP/NM/GW + action bar).
    int _page = 0;

    // Action-bar selection on page 1.
    static constexpr int kBarBack = 0;
    static constexpr int kBarReconnect = 1;
    static constexpr int kBarCount = 2;
    int _barSel = kBarBack;

    // Reconnect flow: the actual connect blocks the main loop (freezing the
    // panel), so pressing "Reconnect" only switches to the status page and
    // *defers* the connect by a frame -- long enough for the "Reconnecting..."
    // status page to paint and push first. _reconnecting drives that label
    // (overriding the possibly-stale polled state) until the link is back up.
    bool _reconnecting = false;
    uint32_t _reconnectFireMs = 0;

    // One-shot callback run a fixed wall-clock delay after it's armed -- long
    // enough for the screen it was queued from to actually paint AND be pushed
    // to the panel (the framebuffer DMA is rate-limited to ~33ms, and a couple
    // of frames elapse in microseconds, so a frame *count* wouldn't guarantee a
    // push). Used to defer the blocking reconnect; general enough for any
    // "repaint, then act" button handler.
    void (*_deferredAction)(WiFiScreen *self) = nullptr;
    uint32_t _deferredDueMs = 0;
    void deferAction(void (*fn)(WiFiScreen *self), uint32_t delayMs);
    static void runReconnect(WiFiScreen *self);

    void poll();
    void back();              // leave the screen entirely (-> Settings)
    void showPage(int page);  // switch between page 0 and page 1
    void drawStatusPage();
    void drawDetailsPage();
    void drawActionBar();
    void activateBar();       // run the selected page-1 action
};

}  // namespace zuluide::display
