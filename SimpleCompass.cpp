#include <Debug.h>

#include <kenshi/CameraClass.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/KingOfRenderThread.h>
#include <kenshi/gui/TitleScreen.h>

#include <mygui/MyGui_Types.h>

#include <core/Functions.h>

#include <boost/math/constants/constants.hpp>

#include <string>

// Compass widgets
MyGUI::Button *g_compass_button = NULL;
static const float kCompassButtonX = 0.5f;  // Centered horizontally
static const float kCompassButtonY = 0.01f; // Near the top of the screen
static const float kCompassButtonWidthFull = 0.20f;
static const float kCompassButtonWidthNumbers = 0.05f;
static const float kCompassButtonWidthDirection = kCompassButtonWidthNumbers * 0.5f;
static const float kCompassButtonHeightTriple = 0.055f;
static const float kCompassButtonHeightSingle = 0.02f;

enum CompassMode
{
    CompassMode_Full = 0, // Full compass: rotating ticks + caret + number
    CompassMode_NumberWithDirection,
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
    case CompassMode_Full:
        return "Full";
    case CompassMode_NumberWithDirection:
        return "Number with Direction";
    case CompassMode_DirectionOnly:
        return "Direction Only";
    default:
        return "Unknown";
    }
}

// Builds a rotating compass bar string with all 24 tick labels.
// Each label is padded to kLabelWidth chars (centered) so the visual width
// is uniform. The array is rotated so the tick closest to yaw_deg sits at the
// center of the string. Together with center-aligned text on the button, this
// makes the visible window track the camera direction.
std::string BuildRotatingCompassString(float yaw_deg)
{
    static const int kLabelWidth = 4;
    static const char *kTickLabels[24] = {"N", "15",  "30",  "NE", "60",  "75",  "E", "105", "120", "SE", "150", "165",
                                          "S", "195", "210", "SW", "240", "255", "W", "285", "300", "NW", "330", "345"};

    // Which tick (0-23) is closest to the current yaw
    int center_tick = static_cast<int>((yaw_deg + 7.5f) / 15.0f) % 24;

    // Rotate so that center_tick ends up at index 12 (middle of 24)
    int start_idx = (center_tick - 12 + 24) % 24;

    std::string result;
    for (int i = 0; i < 24; ++i)
    {
        int idx = (start_idx + i) % 24;
        std::string label = kTickLabels[idx];

        // Center the label in a fixed-width field
        int left_pad = (kLabelWidth - static_cast<int>(label.size())) / 2;
        int right_pad = kLabelWidth - static_cast<int>(label.size()) - left_pad;
        for (int p = 0; p < left_pad; ++p)
        {
            result.push_back(' ');
        }
        result += label;
        for (int p = 0; p < right_pad; ++p)
        {
            result.push_back(' ');
        }
    }
    return result;
}

// Click handler to cycle compact mode
void OnCompassClick(MyGUI::WidgetPtr sender)
{
    auto nextMode = (static_cast<int>(g_compact_mode) + 1) % static_cast<int>(CompassMode_Count);
    g_compact_mode = static_cast<CompassMode>(nextMode);
    float new_width = (g_compact_mode == CompassMode_Full)            ? kCompassButtonWidthFull
                      : (g_compact_mode == CompassMode_DirectionOnly) ? kCompassButtonWidthDirection
                                                                      : kCompassButtonWidthNumbers;
    float new_height = (g_compact_mode == CompassMode_Full) ? kCompassButtonHeightTriple : kCompassButtonHeightSingle;
    g_compass_button->setRealSize(new_width, new_height);
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
    std::string caption;

    if (g_compact_mode == CompassMode_Full)
    {
        // Three-line layout:
        //   Line 1: rotating tick labels (centered on current yaw)
        //   Line 2: caret pointing at the current value
        //   Line 3: exact yaw + direction
        std::string rotating = BuildRotatingCompassString(yaw);
        std::string caret = "^";

        std::ostringstream num_oss;
        num_oss << static_cast<int>(yaw) << "(" << DirectionName(yaw) << ")";
        std::string number = num_oss.str();

        caption = rotating + "\n" + caret + "\n" + number;
    }
    else if (g_compact_mode == CompassMode_NumberWithDirection)
    {
        std::ostringstream oss;
        oss << static_cast<int>(yaw) << "(" << DirectionName(yaw) << ")";
        caption = oss.str();
    }
    else if (g_compact_mode == CompassMode_DirectionOnly) { caption = DirectionName(yaw); }
    else
    {
        caption = "Unknown Mode";
    }

    g_compass_button->setCaption(caption);
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

    // Clickable button to cycle compass mode
    MyGUI::FloatCoord button_coord(
        kCompassButtonX, kCompassButtonY, kCompassButtonWidthNumbers, kCompassButtonHeightSingle
    );
    g_compass_button = gui->createWidgetReal<MyGUI::Button>(
        "Kenshi_Button1", button_coord, MyGUI::Align::Default, "Window", "CompassButton"
    );
    g_compass_button->setTextAlign(MyGUI::Align::Center);
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