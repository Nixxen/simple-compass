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
MyGUI::Button *g_compass_button = nullptr;
static const float kCompassButtonX = 0.5F;  // Centered horizontally
static const float kCompassButtonY = 0.01F; // Near the top of the screen
static const float kCompassButtonWidthFull = 0.20F;
static const float kCompassButtonWidthNumbers = 0.05F;
static const float kCompassButtonWidthDirection = kCompassButtonWidthNumbers * 0.5F;
static const float kCompassButtonHeightTriple = 0.055F;
static const float kCompassButtonHeightSingle = 0.02F;

enum CompassMode
{
    CompassMode_Full = 0, // Full compass: rotating ticks + caret + number
    CompassMode_NumberWithDirection,
    CompassMode_DirectionOnly,
    CompassMode_Count
};
CompassMode g_compact_mode = CompassMode_NumberWithDirection;

static const char kFullString[] =
    "N]-15-30-[NE]-60-75-[E]-105-120-[SE]-150-165-[S]-195-210-[SW]-240-255-[W]-285-300-[NW]-330-345-[";
static const int kFullLength = static_cast<int>(sizeof(kFullString) - 1);
static const float kDegreesPerTick = 15.0F;
static const int kTicksInCompass = 24;

// Character index of each tick label's centre character in kFullString.
// Tick i corresponds to bearing i*15 degrees.
// Label strings for each tick, in order of bearing (0, 15, 30, ...).
static const char *kTickLabels[kTicksInCompass] = {"N",   "15",  "30",  "NE",  "60",  "75",  "E",   "105",
                                                   "120", "SE",  "150", "165", "S",   "195", "210", "SW",
                                                   "240", "255", "W",   "285", "300", "NW",  "330", "345"};

// Runtime-initialised centre character position of each tick label in kFullString.
static float g_tick_char_positions[kTicksInCompass] = {0.0F};
static bool g_tick_positions_initialized = false;

// Per-character pixel widths for kFullString, measured at init.
static float g_char_widths[kFullLength] = {0.0F};
static bool g_pixels_initialized = false;

// Initialises g_tick_char_positions by finding each label in kFullString.
void InitTickPositions()
{
    if (g_tick_positions_initialized) { return; }
    int search_start = 0;
    for (int i = 0; i < kTicksInCompass; ++i)
    {
        const char *found = strstr(kFullString + search_start, kTickLabels[i]);
        if (found == nullptr) { continue; }
        int start = static_cast<int>(found - kFullString);
        int len = static_cast<int>(strlen(kTickLabels[i]));
        g_tick_char_positions[i] = static_cast<float>(start) + (static_cast<float>(len - 1) * 0.5F);
        search_start = start + len;
    }
    g_tick_positions_initialized = true;
}

// Returns yaw in degrees (0-360), 0 = North
float GetYawDegrees()
{
    if (au == nullptr || au->camera == nullptr) { return 0.0F; }
    Ogre::Vector3 facing = au->camera->getFacingDirection();
    float yaw_rad = std::atan2(facing.x, -facing.z); // -Z = North = 0
    float yaw_deg = yaw_rad * 180.0F / boost::math::constants::pi<float>();
    if (yaw_deg < 0.0F) { yaw_deg += 360.0F; }
    return yaw_deg;
}

// Returns the cardinal direction name for a given bearing (0-360)
const char *DirectionName(float bearing)
{
    if (bearing < 22.5F) { return "N"; }
    if (bearing < 67.5F) { return "NE"; }
    if (bearing < 112.5F) { return "E"; }
    if (bearing < 157.5F) { return "SE"; }
    if (bearing < 202.5F) { return "S"; }
    if (bearing < 247.5F) { return "SW"; }
    if (bearing < 292.5F) { return "W"; }
    if (bearing < 337.5F) { return "NW"; }
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

// Measures cumulative pixel widths of kFullString to find the character
// at the pixel centre of the rendered string. Must be called once after
// the button is created.
void InitCharPixelTable()
{
    if (g_compass_button == nullptr) { return; }

    static float pixel_positions[kFullLength + 1] = {0.0F};
    pixel_positions[0] = 0.0F;
    std::string prefix;
    for (int i = 0; i < kFullLength; ++i)
    {
        prefix.push_back(kFullString[i]);
        g_compass_button->setCaption(prefix);
        pixel_positions[i + 1] = static_cast<float>(g_compass_button->getTextSize().width);
    }

    // Restore initial caption
    g_compass_button->setCaption("0(N)");

    // Store per-character widths
    for (int i = 0; i < kFullLength; ++i)
    {
        g_char_widths[i] = pixel_positions[i + 1] - pixel_positions[i];
    }
    g_pixels_initialized = true;
}

// Builds a rotating compass string by sliding a window over kFullString.
// Maps yaw to a character position by interpolating between tick label
// positions. The button clips the string automatically.
std::string BuildRotatingCompassString(float yaw_deg)
{
    if (!g_pixels_initialized) { return std::string(kFullString); }

    // Map yaw to continuous tick index [0..24)
    float tick_f = yaw_deg / kDegreesPerTick;
    int tick_low = static_cast<int>(tick_f) % kTicksInCompass;
    int tick_high = (tick_low + 1) % kTicksInCompass;
    float frac = tick_f - static_cast<float>(static_cast<int>(tick_f));

    // Interpolate character position between adjacent ticks
    float char_pos_f;
    float pos_low = g_tick_char_positions[tick_low];
    float pos_high = g_tick_char_positions[tick_high];
    if (tick_high > tick_low)
    {
        // Normal case
        char_pos_f = pos_low + (frac * (pos_high - pos_low));
    }
    else
    {
        // Wrap around: tick 23 → tick 0
        float dist = static_cast<float>(kFullLength) - pos_low + pos_high;
        char_pos_f = pos_low + (frac * dist);
        if (char_pos_f >= static_cast<float>(kFullLength)) { char_pos_f -= static_cast<float>(kFullLength); }
    }

    int center_idx = static_cast<int>(std::floor(char_pos_f + 0.5F));
    if (center_idx >= kFullLength) { center_idx -= kFullLength; }

    // Build the full string so that center_idx lands at the pixel centre
    // of the rendered text. The button clips the rest.
    std::string result;
    result.reserve(kFullLength);

    // First pass: build a candidate string with center_idx at position 0
    // so we can compute pixel widths of the rotated string.
    static int candidate[kFullLength];
    for (int i = 0; i < kFullLength; ++i)
    {
        int idx = (center_idx + i) % kFullLength;
        if (idx < 0) { idx += kFullLength; }
        candidate[i] = idx;
    }

    // Find the output position whose cumulative pixel width is closest to half
    float total_width = 0.0F;
    for (int i = 0; i < kFullLength; ++i) { total_width += g_char_widths[candidate[i]]; }
    float half = total_width * 0.5F;
    float accum = 0.0F;
    int pixel_center_pos = 0;
    float best_dist = half;
    for (int i = 0; i < kFullLength; ++i)
    {
        accum += g_char_widths[candidate[i]];
        float dist = (accum > half) ? accum - half : half - accum;
        if (dist < best_dist)
        {
            best_dist = dist;
            pixel_center_pos = i;
        }
    }

    // Build the final string with center_idx at pixel_center_pos
    int offset = -pixel_center_pos;
    for (int i = 0; i < kFullLength; ++i)
    {
        int idx = (center_idx + offset + i) % kFullLength;
        if (idx < 0) { idx += kFullLength; }
        result.push_back(kFullString[idx]);
    }

    return result;
}

// Click handler to cycle compact mode
void OnCompassClick(MyGUI::WidgetPtr sender)
{
    auto nextMode = (static_cast<int>(g_compact_mode) + 1) % static_cast<int>(CompassMode_Count);
    g_compact_mode = static_cast<CompassMode>(nextMode);
    float new_width = kCompassButtonWidthNumbers;
    float new_height = kCompassButtonHeightSingle;
    switch (g_compact_mode)
    {
    case CompassMode_Full:
        new_width = kCompassButtonWidthFull;
        new_height = kCompassButtonHeightTriple;
        break;
    case CompassMode_NumberWithDirection:
        new_width = kCompassButtonWidthNumbers;
        break;
    case CompassMode_DirectionOnly:
        new_width = kCompassButtonWidthDirection;
        break;
    }
    g_compass_button->setRealSize(new_width, new_height);
    std::string log_message = std::string("Compass mode changed to ") + CompassModeName(g_compact_mode);
    DebugLog(log_message);
}

void UpdateCompass()
{
    if (g_compass_button == nullptr)
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
TitleScreen *(*TitleScreen_orig)(TitleScreen *) = nullptr;
TitleScreen *TitleScreen_hook(TitleScreen *thisptr)
{
    TitleScreen *title_screen = TitleScreen_orig(thisptr);

    MyGUI::Gui *gui = MyGUI::Gui::getInstancePtr();
    if (gui == nullptr)
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

    InitTickPositions();
    InitCharPixelTable();

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