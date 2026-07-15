#include <Debug.h>

#include <kenshi/CameraClass.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/KingOfRenderThread.h>
#include <kenshi/gui/TitleScreen.h>

#include <mygui/MyGui_Types.h>

#include <core/Functions.h>

#include <boost/math/constants/constants.hpp>

// Compass widgets
MyGUI::Button *g_compass_button = NULL;
static const float kCompassButtonWidthNumbers = 0.05f;
static const float kCompassButtonWidthDirection = kCompassButtonWidthNumbers * 0.5f;

enum CompassMode
{
    CompassMode_NumberWithDirection = 0,
    CompassMode_DirectionOnly,
    CompassMode_Count
};
CompassMode g_compact_mode = CompassMode_NumberWithDirection;

// Returns yaw in degrees (0-360), 0 = North
float GetYawDegrees()
{
    if (!au || !au->camera) { return 0.0f; }
    Ogre::Vector3 facing = au->camera->getFacingDirection();
    float yaw_rad = std::atan2(facing.x, -facing.z); // -Z = North = 0
    float yaw_deg = yaw_rad * 180.0f / boost::math::constants::pi<float>();
    if (yaw_deg < 0.0f) { yaw_deg += 360.0f; }
    return yaw_deg;
}

// Returns the cardinal direction name for a given bearing (0-360)
const char *DirectionName(float bearing)
{
    if (bearing < 22.5f) { return "N"; }
    if (bearing < 67.5f) { return "NE"; }
    if (bearing < 112.5f) { return "E"; }
    if (bearing < 157.5f) { return "SE"; }
    if (bearing < 202.5f) { return "S"; }
    if (bearing < 247.5f) { return "SW"; }
    if (bearing < 292.5f) { return "W"; }
    if (bearing < 337.5f) { return "NW"; }
    return "N";
}

static const char *CompassModeName(CompassMode mode)
{
    switch (mode)
    {
    case CompassMode_NumberWithDirection:
        return "Number with Direction";
    case CompassMode_DirectionOnly:
        return "Direction Only";
    default:
        return "Unknown";
    }
}

// Click handler to cycle compact mode
void OnCompassClick(MyGUI::WidgetPtr sender)
{
    auto nextMode = (static_cast<int>(g_compact_mode) + 1) % static_cast<int>(CompassMode_Count);
    g_compact_mode = static_cast<CompassMode>(nextMode);
    float new_width =
        (g_compact_mode == CompassMode_DirectionOnly) ? kCompassButtonWidthDirection : kCompassButtonWidthNumbers;
    g_compass_button->setRealSize(new_width, 0.02f);
    std::string log_message = std::string("Compass mode changed to ") + CompassModeName(g_compact_mode);
    DebugLog(log_message.c_str());
}

void UpdateCompass()
{
    if (!g_compass_button)
    {
        ErrorLog("Compass button not initialized!");
        return;
    }
    if (g_compass_button->getVisible() == false) { g_compass_button->setVisible(true); }

    float yaw = GetYawDegrees();
    std::ostringstream oss;
    if (g_compact_mode == CompassMode_NumberWithDirection)
    {
        oss << static_cast<int>(yaw) << "(" << DirectionName(yaw) << ")";
    }
    else if (g_compact_mode == CompassMode_DirectionOnly) { oss << DirectionName(yaw); }
    else
    {
        oss << "Unknown Mode";
    }

    g_compass_button->setCaption(oss.str());
}

// Title screen constructor hook
TitleScreen *(*TitleScreen_orig)(TitleScreen *) = NULL;
TitleScreen *TitleScreen_hook(TitleScreen *thisptr)
{
    TitleScreen *title_screen = TitleScreen_orig(thisptr);

    MyGUI::Gui *gui = MyGUI::Gui::getInstancePtr();
    if (!gui)
    {
        ErrorLog("MyGUI::Gui instance not found!");
        return title_screen;
    }

    // Clickable button to cycle compass mode, updating the caption to show current mode
    MyGUI::FloatCoord button_coord(0.5f, 0.01f, kCompassButtonWidthNumbers, 0.02f);
    g_compass_button = gui->createWidgetReal<MyGUI::Button>(
        "Kenshi_Button1", button_coord, MyGUI::Align::Default, "Window", "CompassButton"
    );
    g_compass_button->setCaption("0(N)");
    g_compass_button->setVisible(false);
    g_compass_button->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);

    return title_screen;
}

// Main loop hook
void (*GameWorld_main_loop_orig)(GameWorld *thisptr, float time);
void GameWorld_main_loop_hook(GameWorld *thisptr, float time)
{
    UpdateCompass();
    GameWorld_main_loop_orig(thisptr, time);
}

__declspec(dllexport) void startPlugin()
{
    if (KenshiLib::SUCCESS !=
        KenshiLib::AddHook(KenshiLib::GetRealAddress(&TitleScreen::_CONSTRUCTOR), TitleScreen_hook, &TitleScreen_orig))
    {
        ErrorLog("Could not add TitleScreen hook!");
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                                  KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
                                  GameWorld_main_loop_hook, &GameWorld_main_loop_orig
                              ))
    {
        ErrorLog("Could not add main loop hook!");
    }
}
