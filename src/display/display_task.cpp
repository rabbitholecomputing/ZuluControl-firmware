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
#include "screen_saver.h"
#include "screen_registry.h"
#include "screen_type.h"
#include "splash_screen.h"
#include "ui_settings.h"
#include "../ZuluControlI2CClient.h"  // FW_VERSION
#include "../fw_upgrade.h"            // FwUpgradeStatus / fwupgrade_get_status
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
// firmware version for 1500ms, then the connection status for 1000ms,
// then move on to Main. Runs exactly once at boot; later visits to Splash
// (via Settings' "About") don't re-trigger it since g_bootPhase is
// already Done by then.
enum class BootPhase
{
    ShowVersion,
    ShowConnectionStatus,
    Done
};

BootPhase g_bootPhase = BootPhase::ShowVersion;
uint32_t g_bootPhaseStartMs = 0;

void tickBootSequence()
{
    if (g_bootPhase == BootPhase::Done)
        return;

    uint32_t elapsed = millis() - g_bootPhaseStartMs;

    if (g_bootPhase == BootPhase::ShowVersion && elapsed > 1500)
    {
        g_bootPhase = BootPhase::ShowConnectionStatus;
        g_bootPhaseStartMs = millis();

        auto *splash = static_cast<SplashScreen *>(GetScreen(DisplayScreenType::Splash));
        char banner[40];
        switch (g_data.GetDeviceType())
        {
            case DeviceType::ZuluSCSI: snprintf(banner, sizeof(banner), "Connected: ZuluSCSI"); break;
            case DeviceType::ZuluIDE: snprintf(banner, sizeof(banner), "Connected: ZuluIDE"); break;
            default: snprintf(banner, sizeof(banner), "Searching..."); break;
        }
        splash->setBannerText(banner);
    }
    else if (g_bootPhase == BootPhase::ShowConnectionStatus && elapsed > 1000)
    {
        g_bootPhase = BootPhase::Done;
        ChangeScreen(DisplayScreenType::Main);
    }
}

// Surfaces the status JSON's sdPresent flag -- real information from
// ZuluSCSI/ZuluIDE, not a local decision -- as a MessageBox popup whenever
// it changes, from whatever screen happens to be active (matches
// MessageBox.h/.cpp's role in the original UI: showing information the
// device pushed, not just local warnings).
bool g_sdPresentKnown = false;
bool g_lastSdPresent = false;

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

    Screen *active = GetActiveScreen();
    if (!active || active->screenType() == DisplayScreenType::MessageBox)
        return;  // don't interrupt a message already showing

    ShowMessage("-- Info --", "SD Card", nowPresent ? "Inserted" : "Removed",
                active->screenType(), active->getOriginalIndex(), 2000);
}

void dispatchInput(const InputEvents &events)
{
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
    if (events.longPress[(int)Button::Rotary])
        active->longRotaryPress();
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

    g_initialized = true;
    return true;
}

void DisplayControlTask()
{
    if (!g_initialized)
        return;

    g_data.Refresh();

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
