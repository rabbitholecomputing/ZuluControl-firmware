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

#include "screen_registry.h"
#include "splash_screen.h"
#include "main_screen.h"
#include "browse_screen.h"
#include "settings_screen.h"
#include "info_screen.h"
#include "message_box.h"
#include "placeholder_screens.h"
#include "ide_main_screen.h"
#include "ide_browse_screen.h"
#include "ide_settings_screen.h"
#include "about_screen.h"
#include "menu_screen.h"
#include "firmware_upgrade_screen.h"
#include "../ZuluControl_config.h"  // g_device_type

namespace zuluide::display {

namespace {

Screen *g_activeScreen = nullptr;
int g_previousIndex = -1;

SplashScreen *g_splash = nullptr;
MainScreen *g_main = nullptr;
BrowseScreen *g_browse = nullptr;
SettingsScreen *g_settings = nullptr;
InfoScreen *g_info = nullptr;
MessageBox *g_messageBox = nullptr;

// ZuluIDE variants of Main/Browse/Settings -- GetScreen() returns these
// instead of the SCSI-oriented screens above when the connected device is a
// ZuluIDE (see the DisplayScreenType::Main/Browse/Settings cases). Both sets
// are always allocated; which one is live is a per-request check on
// g_device_type, so a device detected/redetected at runtime picks the right
// UI without re-initializing the registry.
IDEMainScreen *g_ideMain = nullptr;
IDEBrowseScreen *g_ideBrowse = nullptr;
IDESettingsScreen *g_ideSettings = nullptr;
AboutScreen *g_about = nullptr;
MenuScreen *g_menu = nullptr;

PlaceholderScreen *g_infoPage2 = nullptr;
PlaceholderScreen *g_infoPage3 = nullptr;
PlaceholderScreen *g_infoPage4 = nullptr;
PlaceholderScreen *g_browseType = nullptr;
FirmwareUpgradeScreen *g_copy = nullptr;
PlaceholderScreen *g_initiatorMain = nullptr;
PlaceholderScreen *g_noControlsError = nullptr;

bool isIde()
{
    return g_device_type == zulucontrol::config::DeviceType::ZuluIDE;
}

}  // namespace

void InitScreens(Framebuffer128x64 *fb, DisplayData *data)
{
    static SplashScreen splash(fb);
    static MainScreen main(fb, data);
    static BrowseScreen browse(fb, data);
    static SettingsScreen settings(fb, data, &splash);
    static InfoScreen info(fb, data);
    static MessageBox messageBox(fb);

    static IDEMainScreen ideMain(fb, data);
    static IDEBrowseScreen ideBrowse(fb, data);
    static IDESettingsScreen ideSettings(fb, data);
    static AboutScreen about(fb);
    static MenuScreen menu(fb);

    static PlaceholderScreen infoPage2(fb, DisplayScreenType::InfoPage2);
    static PlaceholderScreen infoPage3(fb, DisplayScreenType::InfoPage3);
    static PlaceholderScreen infoPage4(fb, DisplayScreenType::InfoPage4);
    static PlaceholderScreen browseType(fb, DisplayScreenType::BrowseType);
    static FirmwareUpgradeScreen copy(fb);
    static PlaceholderScreen initiatorMain(fb, DisplayScreenType::InitiatorMain);
    static PlaceholderScreen noControlsError(fb, DisplayScreenType::NoControlsError);

    g_splash = &splash;
    g_main = &main;
    g_browse = &browse;
    g_settings = &settings;
    g_info = &info;
    g_messageBox = &messageBox;
    g_ideMain = &ideMain;
    g_ideBrowse = &ideBrowse;
    g_ideSettings = &ideSettings;
    g_about = &about;
    g_menu = &menu;
    g_infoPage2 = &infoPage2;
    g_infoPage3 = &infoPage3;
    g_infoPage4 = &infoPage4;
    g_browseType = &browseType;
    g_copy = &copy;
    g_initiatorMain = &initiatorMain;
    g_noControlsError = &noControlsError;
}

Screen *GetScreen(DisplayScreenType type)
{
    switch (type)
    {
        case DisplayScreenType::Splash: return g_splash;
        case DisplayScreenType::Main: return isIde() ? (Screen *)g_ideMain : (Screen *)g_main;
        case DisplayScreenType::Browse: return isIde() ? (Screen *)g_ideBrowse : (Screen *)g_browse;
        case DisplayScreenType::Settings: return isIde() ? (Screen *)g_ideSettings : (Screen *)g_settings;
        case DisplayScreenType::Info: return g_info;
        case DisplayScreenType::About: return g_about;
        case DisplayScreenType::Menu: return g_menu;
        case DisplayScreenType::InfoPage2: return g_infoPage2;
        case DisplayScreenType::InfoPage3: return g_infoPage3;
        case DisplayScreenType::InfoPage4: return g_infoPage4;
        case DisplayScreenType::BrowseType: return g_browseType;
        case DisplayScreenType::MessageBox: return g_messageBox;
        case DisplayScreenType::Copy: return g_copy;
        case DisplayScreenType::InitiatorMain: return g_initiatorMain;
        case DisplayScreenType::NoControlsError: return g_noControlsError;
        default: return nullptr;
    }
}

void ChangeScreen(DisplayScreenType type, int index)
{
    Screen *s = GetScreen(type);
    if (!s)
        return;

    if (index == -1)
        index = g_previousIndex;

    g_activeScreen = s;
    s->init(index);
    g_previousIndex = index;
}

Screen *GetActiveScreen()
{
    return g_activeScreen;
}

void ShowMessage(const char *title, const char *line1, const char *line2,
                  DisplayScreenType returnScreen, int returnIndex, uint32_t autoCloseMs)
{
    g_messageBox->SetText(title, line1, line2);
    g_messageBox->SetReturnScreen(returnScreen, returnIndex);
    g_messageBox->SetAutoCloseMs(autoCloseMs);
    ChangeScreen(DisplayScreenType::MessageBox, returnIndex);
}

}  // namespace zuluide::display
