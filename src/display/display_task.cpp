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

#include "display_task.h"
#include "framebuffer.h"
#include "ssd1306.h"
#include "gpio_expander.h"
#include "display_data.h"
#include "screen.h"                   // DrawLoadingPopup
#include "screen_saver.h"
#include "screen_registry.h"
#include "screen_type.h"
#include "splash_screen.h"
#include "ui_settings.h"
#include "../ZuluControlI2CClient.h"  // FW_VERSION
#include "../fw_upgrade.h"            // FwUpgradeStatus / fwupgrade_get_status
#include "../webui_data.h"            // IsFilenamesFetchActive / GetFilenamesBytesReceived
#include <hardware/i2c.h>
#include <hardware/sync.h>           // save_and_disable_interrupts (pump guard)
#include <pico/time.h>
#include <pico/platform.h>           // tight_loop_contents
#include <cstdio>
#include <cstring>

namespace zuluide::display {

namespace {

// New bus, independent of the i2c0 slave link to ZuluSCSI/ZuluIDE
// (GPIO0/1) -- confirmed with the user: same panel hardware as ZuluSCSI's
// control board (SSD1306 @0x3C + I2C GPIO-expander @0x3F), on i2c1's
// default SDA/SCL pins.
constexpr unsigned int kSdaPin = 2;
constexpr unsigned int kSclPin = 3;
constexpr unsigned int kBaudrate = 400000;
constexpr uint8_t kDisplayAddr = 0x3C;
constexpr uint8_t kExpanderAddr = 0x3F;

// SSD1306 framebuffer pushes (1024 bytes) are large enough to matter to
// core0's lwIP polling, so they're rate-limited independently of how often
// screen content is recomputed (cheap, no I2C) -- ~30Hz is plenty for this
// UI's animation rates (the fastest screen saver ticks every 40ms anyway).
constexpr uint32_t kPushIntervalMs = 33;

// Minimum spacing between the *blocking* firmware-upgrade pushes (see
// PumpFirmwareUpgradeDisplay). Longer than kPushIntervalMs because each of
// these ties up the bus for a whole ~23ms frame, and during a web upload
// they run in the lwIP background context -- too-frequent pushes would slow
// the transfer noticeably. ~10Hz is smooth enough for a progress bar.
constexpr uint32_t kFwPushIntervalMs = 100;

// How long the completed "X/Y" filenames-loading popup stays visible after
// the fetch actually finishes, before being dismissed -- long enough to
// read the final byte count rather than having it vanish the instant the
// closing sentinel arrives.
constexpr uint32_t kLoadingHoldMs = 2000;

// SD Card Inserted/Removed message's own on-screen duration.
constexpr uint32_t kSdMessageMs = 1000;

Framebuffer128x64 g_fb;
Ssd1306Display g_display;
GpioExpander g_expander(kExpanderAddr);
DisplayData g_data;
ScreenSaverController g_screenSaver(&g_fb, &g_display);

// Last framebuffer contents actually pushed to the panel. A push chunks the
// whole 1024-byte frame across ~2.9ms DMA bursts on the shared i2c1 bus, and
// the rotary encoder can't be read while one of those bursts is streaming --
// so pushing a frame identical to what's already on screen (e.g. sitting
// still in a menu) would blind the encoder for ~23ms every kPushIntervalMs
// for nothing. Only push when the frame has actually changed; a static screen
// then leaves the bus free for tight, uninterrupted encoder polling.
uint8_t g_lastPushed[Framebuffer128x64::SIZE_BYTES];
bool g_havePushed = false;

uint32_t g_lastPushMs = 0;
bool g_initialized = false;

uint32_t millis()
{
    return to_ms_since_boot(get_absolute_time());
}

// Boot-time banner sequence, external to SplashScreen itself (which is a
// "dumb" screen, see splash_screen.h) -- mirrors control.cpp's
// splashScreenPoll()/ZULUSCSI_UI_START state machine: this project's own
// firmware version for at least 1500ms, then the connection status for at
// least 1000ms more -- but the move to Main additionally waits on
// HasReceivedStatus(), so the splash holds indefinitely if the first real
// device status hasn't arrived yet by the time that 1000ms elapses, rather
// than dropping to a still-empty Main screen. Runs exactly once at boot;
// later visits to Splash (via Settings' "About") don't re-trigger it since
// g_bootPhase is already Done by then.
enum class BootPhase
{
    ShowVersion,
    ShowConnectionStatus,
    Done
};

BootPhase g_bootPhase = BootPhase::ShowVersion;
uint32_t g_bootPhaseStartMs = 0;

// "Searching..." is the only thing this banner ever says -- it's only
// meaningful while the device type is still Unknown. Once it resolves, the
// logo swap (SplashScreen::draw()'s device_type switch) and the I2C API
// sub-banner (updateApiSubBanner()) already say everything there is to
// say, so there's no "Connected: ZuluSCSI"/"Connected: ZuluIDE" text at all
// -- this just clears the banner instead.
void updateConnectionBanner(SplashScreen *splash, DeviceType type)
{
    splash->setBannerText(type == DeviceType::Unknown ? "Searching..." : "");
}

// Normally "I2C API vX.X.X" (this client's own version), but if the host's
// major version doesn't match ours, "API ZS: vA.B.C ZC: vX.X.X" instead --
// ZS/ZI for the host (ZuluSCSI/ZuluIDE), ZC for this client (ZuluControl) --
// so a mismatch worth fixing is visible on the boot splash itself, not just
// in the browser-facing version JSON's "message" field.
void updateApiSubBanner(SplashScreen *splash)
{
    char apiBanner[40];
    const char *serverVersion = GetServerAPIVersionOnly();
    if (IsApiVersionMismatch() && serverVersion[0] != '\0')
    {
        const char *hostAbbrev = (g_data.GetDeviceType() == DeviceType::ZuluIDE) ? "ZI" : "ZS";
        snprintf(apiBanner, sizeof(apiBanner), "API %s: v%s ZC: v%s",
                 hostAbbrev, serverVersion, I2C_API_VERSION);
    }
    else
    {
        snprintf(apiBanner, sizeof(apiBanner), "I2C API v%s", I2C_API_VERSION);
    }
    splash->setSubBannerText(apiBanner);
}

// The local I2C API version line is deliberately withheld until the host
// (ZuluSCSI/ZuluIDE) has sent its own API version and g_data.GetDeviceType()
// resolves away from Unknown -- until then the generic ZuluControl logo is
// showing (SplashScreen::draw()'s device_type switch), and this project's
// own API version isn't meaningful information about a device we haven't
// heard from yet. The moment it resolves, SplashScreen::draw() also starts
// drawing the real ZuluSCSI/ZuluIDE logo instead, on the very same
// forceDraw() setSubBannerText() triggers -- so the logo swap and the
// sub-banner appearing happen together, in one redraw.
bool g_apiSubBannerSet = false;

void tickBootSequence()
{
    if (g_bootPhase == BootPhase::Done)
        return;

    if (!g_apiSubBannerSet && g_data.GetDeviceType() != DeviceType::Unknown)
    {
        g_apiSubBannerSet = true;
        auto *splash = static_cast<SplashScreen *>(GetScreen(DisplayScreenType::Splash));
        updateApiSubBanner(splash);

        if (g_bootPhase == BootPhase::ShowConnectionStatus)
        {
            // The device type resolved after this phase's own banner text
            // was already set to "Searching..." -- clear it now, rather
            // than leaving it stale next to the now-correct logo and
            // sub-banner.
            updateConnectionBanner(splash, g_data.GetDeviceType());
        }
    }

    uint32_t elapsed = millis() - g_bootPhaseStartMs;

    if (g_bootPhase == BootPhase::ShowVersion && elapsed > 1500)
    {
        g_bootPhase = BootPhase::ShowConnectionStatus;
        g_bootPhaseStartMs = millis();

        auto *splash = static_cast<SplashScreen *>(GetScreen(DisplayScreenType::Splash));
        updateConnectionBanner(splash, g_data.GetDeviceType());
    }
    else if (g_bootPhase == BootPhase::ShowConnectionStatus && elapsed > 1000 && HasReceivedStatus())
    {
        g_bootPhase = BootPhase::Done;
        ChangeScreen(DisplayScreenType::Main);
    }
}

// Draws over whatever screen is active, every tick a filenames fetch is in
// flight (IsFilenamesFetchActive()) -- not just on a state transition like
// checkSdStatusChange(), since this popup needs to keep showing for the
// fetch's whole duration. Deliberately not a MessageBox/ChangeScreen(): the
// server can push a filenames update completely unprompted (e.g. right
// after boot), with no screen having called RequestFilenames(), so this
// can't be driven from any one screen's own tick()/draw() -- it has to be
// a display_task-level overlay that works no matter which screen is
// showing (Main, Browse, Settings, ...).
bool g_filenamesFetchWasActive = false;

// True from the moment a fetch finishes until kLoadingHoldMs later -- the
// popup keeps showing (frozen at the final byte count, since
// GetFilenamesBytesReceived() isn't reset until the next fetch starts)
// through this window instead of disappearing the instant the closing
// sentinel arrives.
bool g_filenamesHolding = false;
uint32_t g_filenamesHoldUntilMs = 0;

// True while the filenames-loading popup is either actively fetching or
// still in its post-fetch hold -- i.e. whenever it's actually visible on
// screen (modulo the SD-present/MessageBox draw-site checks in
// overlayFilenamesLoading() itself). IsFilenamesFetchActive() alone isn't
// enough for callers like checkSdStatusChange(): a fetch with a zero-length
// payload (e.g. the server clearing its filenames cache right as the SD
// card is pulled) can go active -> finished within a single display tick,
// so by the time checkSdStatusChange() next runs, IsFilenamesFetchActive()
// already reads false even though the popup is still frozen on screen for
// its hold window -- checking this instead catches that case too.
bool IsFilenamesPopupActive()
{
    return IsFilenamesFetchActive() || g_filenamesHolding;
}

// Surfaces the status JSON's sdPresent flag -- real information from
// ZuluSCSI/ZuluIDE, not a local decision -- as a MessageBox popup whenever
// it changes, from whatever screen happens to be active (matches
// MessageBox.h/.cpp's role in the original UI: showing information the
// device pushed, not just local warnings).
bool g_sdPresentKnown = false;
bool g_lastSdPresent = false;

// Set when an SD status change lands while the filenames-loading overlay
// (below) is showing, so it can be shown right after that overlay clears
// instead of ChangeScreen()-ing to MessageBox on top of it (MessageBox
// only draws its own box, it doesn't clear first, so stacking it over the
// loading popup would leave a corrupted mix of both on screen).
bool g_pendingSdMessage = false;
bool g_pendingSdPresent = false;

void showSdMessage(bool nowPresent)
{
    Screen *active = GetActiveScreen();
    if (!active || active->screenType() == DisplayScreenType::MessageBox)
        return;  // don't interrupt a message already showing

    ShowMessage("-- Info --", "SD Card", nowPresent ? "Inserted" : "Removed",
                active->screenType(), active->getOriginalIndex(), kSdMessageMs);
}

void checkSdStatusChange()
{
    if (g_bootPhase != BootPhase::Done)
        return;  // don't interrupt the boot sequence

    bool nowPresent = g_data.SdPresent();
    if (!g_sdPresentKnown)
    {
        g_lastSdPresent = nowPresent;
        g_sdPresentKnown = true;
        return;
    }
    if (nowPresent == g_lastSdPresent)
        return;
    g_lastSdPresent = nowPresent;

    if (IsFilenamesPopupActive())
    {
        // Queue it -- overlayFilenamesLoading() shows it once the fetch
        // (and its post-fetch hold) finishes and the popup is off screen.
        g_pendingSdMessage = true;
        g_pendingSdPresent = nowPresent;
        return;
    }

    showSdMessage(nowPresent);
}

void overlayFilenamesLoading()
{
    bool fetchActive = IsFilenamesFetchActive();
    uint32_t now = millis();
    bool showPopup = fetchActive;

    if (fetchActive)
    {
        g_filenamesFetchWasActive = true;
        // A new fetch started before the previous hold elapsed (back-to-
        // back fetches) -- drop the hold and just keep showing progress.
        g_filenamesHolding = false;
    }
    else if (g_filenamesFetchWasActive)
    {
        g_filenamesFetchWasActive = false;
        g_filenamesHolding = true;
        g_filenamesHoldUntilMs = now + kLoadingHoldMs;
    }

    if (g_filenamesHolding)
    {
        if ((int32_t)(now - g_filenamesHoldUntilMs) < 0)
        {
            showPopup = true;  // still holding the completed state visible
        }
        else
        {
            g_filenamesHolding = false;

            // Hold elapsed -- force a full, synchronous redraw so the
            // popup's last frame doesn't linger on top of an otherwise-
            // static screen (mirrors the screen-saver-exit redraw below),
            // then flush any SD-status message that queued up while the
            // popup was covering the screen.
            Screen *screen = GetActiveScreen();
            if (screen)
            {
                screen->forceDraw();
                screen->tick();
            }
            if (g_pendingSdMessage)
            {
                g_pendingSdMessage = false;
                showSdMessage(g_pendingSdPresent);
            }
        }
    }

    if (!showPopup)
        return;

    if (g_bootPhase != BootPhase::Done)
        return;  // the version/connection banner owns the screen until then

    Screen *screen = GetActiveScreen();
    if (!screen || screen->screenType() == DisplayScreenType::MessageBox)
        return;  // don't stack over a message already showing

    if (!g_data.SdPresent())
        return;  // nothing meaningful to fetch a file list for without an SD card

    DrawLoadingPopup(&g_fb, "File list Loading", GetFilenamesBytesReceived(), FILENAMES_JSON_CACHE_SIZE);
}

// Long-press-of-rotary recovery gesture: re-initialize the SSD1306 (in case a
// glitch on the shared i2c1 bus left the panel mis-configured -- wrong
// addressing window, charge pump off, garbled pixels) and force the active
// screen to redraw from scratch, so a corrupted display can be recovered
// without power-cycling the board. Handled here at the task level rather than
// in any one Screen so it behaves identically from every screen.
void resetAndRedrawDisplay()
{
    // Drain any in-flight async push first -- Ssd1306Display::Reset()
    // reconfigures the addressing window, so a page still streaming from the
    // old frame must not land mid-reconfigure.
    while (g_display.IsBusy())
        g_display.Poll();

    g_display.Reset();

    // Reset() only reconfigures the controller; GDDRAM still holds whatever
    // (possibly garbled) pixels were there. Force the whole frame to be
    // regenerated and re-pushed even if the framebuffer bytes are unchanged --
    // g_havePushed = false defeats the change-gate in pollAndPush() so the
    // full frame is streamed back out.
    g_havePushed = false;

    Screen *active = GetActiveScreen();
    if (active)
        active->forceDraw();  // regenerated + pushed by the normal tick/pollAndPush path
}

void dispatchInput(const InputEvents &events)
{
    // Global display-recovery gesture -- takes precedence over any screen's own
    // long-rotary handling (see resetAndRedrawDisplay()); consumes the event so
    // it isn't also delivered to the active screen.
    if (events.longPress[(int)Button::Rotary])
    {
        resetAndRedrawDisplay();
        return;
    }

    Screen *active = GetActiveScreen();
    if (!active)
        return;

    if (events.rotaryDirection != 0)
        active->rotaryChange(events.rotaryDirection);
    if (events.shortPress[(int)Button::Eject])
        active->shortEjectPress();
    if (events.shortPress[(int)Button::Insert])
        active->shortUserPress();
    if (events.shortPress[(int)Button::Rotary])
        active->shortRotaryPress();
    if (events.longPress[(int)Button::Eject])
        active->longEjectPress();
    if (events.longPress[(int)Button::Insert])
        active->longUserPress();
}

bool hasAnyInput(const InputEvents &events)
{
    return events.rotaryDirection != 0 ||
           events.shortPress[0] || events.shortPress[1] || events.shortPress[2] ||
           events.longPress[0] || events.longPress[1] || events.longPress[2];
}

// Advance any in-flight DMA push and, if the cadence gate is open and the
// frame actually changed, kick off a new one. Shared by the normal tick path
// and the firmware-upgrade path so there's a single implementation of the
// change-gated push (see g_lastPushed for why redundant pushes are skipped).
void pollAndPush()
{
    g_display.Poll();

    uint32_t now = millis();
    if (!g_display.IsBusy() && (now - g_lastPushMs) >= kPushIntervalMs)
    {
        bool changed = !g_havePushed ||
                       memcmp(g_lastPushed, g_fb.buffer, sizeof(g_lastPushed)) != 0;
        if (changed && g_display.StartPush(g_fb))
        {
            memcpy(g_lastPushed, g_fb.buffer, sizeof(g_lastPushed));
            g_havePushed = true;
        }
        g_lastPushMs = now;
    }
}

}  // namespace

int GetFilenameIndexUsed()
{
    return g_data.IndexedFilenameTotal();
}

int GetFilenameIndexCapacity()
{
    return DisplayData::MAX_FILENAME_INDEX;
}

bool InitDisplayControl()
{
    if (!g_display.Init(i2c1, kSdaPin, kSclPin, kBaudrate, kDisplayAddr))
        return false;

    g_screenSaver.SetConfiguredStyle(ScreenSaverType::Random);  // task requirement: default is random selection

    InitScreens(&g_fb, &g_data);

    auto *splash = static_cast<SplashScreen *>(GetScreen(DisplayScreenType::Splash));
    splash->setBannerText(FW_VERSION);
    ChangeScreen(DisplayScreenType::Splash);
    g_bootPhase = BootPhase::ShowVersion;
    g_bootPhaseStartMs = millis();
    g_apiSubBannerSet = false;  // tickBootSequence() sets it once the host's API version resolves the device type

    g_initialized = true;
    return true;
}

void DisplayControlTask()
{
    // Kept ahead of the g_initialized guard: DisplayData is a pure parse of the
    // JSON webui_data.h already caches -- it touches neither the panel nor i2c1
    // -- and its filename index also backs the web UI's /usage.json. Behind the
    // guard, a board with no control panel attached (InitDisplayControl()
    // having returned false) never rebuilt the index at all, so that endpoint's
    // "imagesUsed" -- the UI's "Cache index" line -- read 0 forever no matter
    // how many filenames were actually cached.
    g_data.Refresh();

    if (!g_initialized)
        return;

    // Firmware upgrade in progress: the "Firmware Upgrade" progress screen
    // takes over the panel until the board reboots into the new image (or the
    // upgrade is aborted). It pre-empts the boot sequence, SD-status popups,
    // input dispatch and the screen saver -- nothing should displace or dim
    // this display while flash is being written. Both the HTTP and I2C upgrade
    // paths feed fwupgrade_get_status() (see fw_upgrade.cpp).
    FwUpgradeStatus fw;
    fwupgrade_get_status(&fw);
    if (fw.active)
    {
        // Keep the idle timer fresh so the saver doesn't fire the instant the
        // upgrade ends and normal ticking resumes.
        g_screenSaver.NotifyUserInput();
        // Render + push the progress screen. The same guarded, blocking pump
        // is used here and from the web-upload receive callback so the two
        // never collide on i2c1 (see PumpFirmwareUpgradeDisplay). It owns the
        // screen switch and the push, so nothing else runs this tick.
        PumpFirmwareUpgradeDisplay();
        return;
    }

    // Upgrade just ended without a reboot (aborted/interrupted): drop the
    // progress screen and return to Main.
    {
        Screen *active = GetActiveScreen();
        if (active && active->screenType() == DisplayScreenType::Copy)
            ChangeScreen(DisplayScreenType::Main);
    }

    // Keep the controller's style in sync with the Settings screen's choice
    // (cheap; decoupled via ui_settings so screens need no controller handle),
    // and honor a pending "Turn on screen saver" menu request.
    g_screenSaver.SetConfiguredStyle(GetConfiguredScreenSaver());
    if (ConsumeScreenSaverNowRequest())
        g_screenSaver.ForceActivate();

    tickBootSequence();
    checkSdStatusChange();

    InputEvents events;
    bool gotInput = g_expander.Poll(events);
    bool anyInput = gotInput && hasAnyInput(events);
    if (anyInput)
        g_screenSaver.NotifyUserInput();

    bool wasActive = g_screenSaver.IsActive();
    g_screenSaver.Tick();
    bool stillActive = g_screenSaver.IsActive();

    if (wasActive && !stillActive)
    {
        // Screen saver just exited -- first press only wakes the display
        // (control.cpp:1341-1386's behavior), and the underlying screen
        // needs a fresh redraw since its framebuffer content was overwritten.
        Screen *active = GetActiveScreen();
        if (active)
            active->forceDraw();
    }
    else if (!wasActive && gotInput)
    {
        dispatchInput(events);
    }

    if (!stillActive)
    {
        Screen *active = GetActiveScreen();
        if (active)
            active->tick();

        overlayFilenamesLoading();
    }

    pollAndPush();
}

void PumpFirmwareUpgradeDisplay()
{
    if (!g_initialized)
        return;

    FwUpgradeStatus fw;
    fwupgrade_get_status(&fw);
    if (!fw.active)
        return;

    // Reentrancy / bus-ownership guard. This is called from core0's main loop
    // AND from the lwIP background context (which preempts the main loop). They
    // can never run truly in parallel on one core, but the background context
    // can preempt the main loop mid-pump -- so whoever is already inside owns
    // i2c1 until it finishes, and the other returns rather than colliding on
    // the shared bus / DMA channels.
    static volatile bool s_busy = false;
    uint32_t irqState = save_and_disable_interrupts();
    if (s_busy)
    {
        restore_interrupts(irqState);
        return;
    }
    s_busy = true;
    restore_interrupts(irqState);

    // Throttle the blocking pushes (each occupies the bus for a whole frame).
    static uint32_t s_lastMs = 0;
    static bool s_pushedOnce = false;
    uint32_t now = millis();
    if (s_pushedOnce && (now - s_lastMs) < kFwPushIntervalMs)
    {
        s_busy = false;
        return;
    }

    // Own the progress screen (lazily, so a web upload that starves the main
    // loop still gets it switched in from here).
    Screen *screen = GetScreen(DisplayScreenType::Copy);
    if (GetActiveScreen() != screen)
        ChangeScreen(DisplayScreenType::Copy);

    // Drain any async (DMA) push left in flight from the pre-upgrade frame so
    // StartPush() below is free to begin, then render current progress and
    // push it out synchronously (no DMA left running across the guard).
    while (g_display.IsBusy())
        g_display.Poll();

    screen = GetActiveScreen();
    if (screen)
    {
        screen->forceDraw();
        screen->tick();
    }

    if (g_display.StartPush(g_fb))
    {
        while (!g_display.Poll())
            tight_loop_contents();
        memcpy(g_lastPushed, g_fb.buffer, sizeof(g_lastPushed));
        g_havePushed = true;
    }

    s_lastMs = now;
    s_pushedOnce = true;
    s_busy = false;
}

}  // namespace zuluide::display
