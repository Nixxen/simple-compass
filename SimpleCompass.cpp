#include <Debug.h>

#include <kenshi/CameraClass.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/KingOfRenderThread.h>
#include <kenshi/gui/TitleScreen.h>

#include <mygui/MyGui_Types.h>

#include <core/Functions.h>

#include <algorithm>
#include <boost/math/constants/constants.hpp>

// Compass widgets
MyGUI::Button *g_compass_button = nullptr;
MyGUI::TextBox *g_compass_left = nullptr;
MyGUI::TextBox *g_compass_center = nullptr;
MyGUI::TextBox *g_compass_right = nullptr;
MyGUI::TextBox *g_compass_caret = nullptr;
static const float kCompassButtonX = 0.5F;  // Centered horizontally
static const float kCompassButtonY = 0.01F; // Near the top of the screen
static const float kCompassButtonWidthFull = 0.20F;
static const float kCompassButtonWidthNumbers = 0.05F;
static const float kCompassButtonWidthDirection = kCompassButtonWidthNumbers * 0.5F;
static const float kCompassButtonHeightDouble = 0.04F;
static const float kCompassButtonHeightSingle = 0.02F;

static const MyGUI::Colour &GetCompassTextColour()
{
    static const MyGUI::Colour colour(1.0F, 1.0F, 0.0F); // Yellow
    return colour;
}

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

// Label strings for each tick, in order of bearing (0, 15, 30, ...).
static const char *kTickLabels[kTicksInCompass] = {"N",   "15",  "30",  "NE",  "60",  "75",  "E",   "105",
                                                   "120", "SE",  "150", "165", "S",   "195", "210", "SW",
                                                   "240", "255", "W",   "285", "300", "NW",  "330", "345"};

// Runtime-initialised centre character position of each tick label in kFullString.
static float g_tick_char_positions[kTicksInCompass] = {0.0F};
static bool g_tick_positions_initialized = false;

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

// Maps yaw to a character position in kFullString by interpolating between
// tick label centre positions. Returns the interpolated float character index.
float GetCenterCharIndex(float yaw_deg)
{
    float tick_f = yaw_deg / kDegreesPerTick;
    int tick_low = static_cast<int>(tick_f) % kTicksInCompass;
    int tick_high = (tick_low + 1) % kTicksInCompass;
    float frac = tick_f - static_cast<float>(static_cast<int>(tick_f));

    float pos_low = g_tick_char_positions[tick_low];
    float pos_high = g_tick_char_positions[tick_high];

    if (tick_high > tick_low) { return pos_low + (frac * (pos_high - pos_low)); }

    // Wrap around: tick 23 → tick 0
    float dist = static_cast<float>(kFullLength) - pos_low + pos_high;
    float result = pos_low + (frac * dist);
    if (result >= static_cast<float>(kFullLength)) { result -= static_cast<float>(kFullLength); }
    return result;
}

// Builds the three compass substrings for the tiled layout.
// center_idx is the integer character index at the centre of the window.
// The center textbox gets exactly 1 character (the semantic character at the
// exact facing direction). Left and right each get kFullLength/2 characters
// collected outward from center, with left reversed for right-aligned display.
void BuildCompassSubstrings(int center_idx, std::string &left_out, std::string &center_out, std::string &right_out)
{
    center_out.clear();
    center_out.push_back(kFullString[center_idx]);

    int half_len = kFullLength / 2;

    // Left: walk backwards from center_idx-1, collect half_len chars, then reverse
    left_out.clear();
    for (int i = 0; i < half_len; ++i)
    {
        int idx = (center_idx - 1 - i + kFullLength) % kFullLength;
        left_out.push_back(kFullString[idx]);
    }
    std::reverse(left_out.begin(), left_out.end());

    // Right: walk forwards from center_idx+1, collect half_len chars
    right_out.clear();
    for (int i = 0; i < half_len; ++i)
    {
        int idx = (center_idx + 1 + i) % kFullLength;
        right_out.push_back(kFullString[idx]);
    }
}

// Creates or repositions the compass line text widgets to match the button's current size.
void SetupCompassLineWidgets()
{
    if (g_compass_button == nullptr || g_compass_left == nullptr || g_compass_center == nullptr ||
        g_compass_right == nullptr || g_compass_caret == nullptr)
    {
        ErrorLog("One or more compass  widgets are not initialized!");
        return;
    }

    static const int button_width = g_compass_button->getWidth();
    static const int button_h = g_compass_button->getHeight();

    static const int button_padding = 5; // Rough spacing from outsie edge to inside button
    static const int text_height = g_compass_left->getFontHeight();
    static const int first_line_y = button_padding;
    // Almost overlap with scrolling text
    static const int caret_line_y = first_line_y + (text_height / 2);
    // 1 character wide. Estimate width to be roughly equal to font height, minus padding
    static const int center_width = static_cast<int>(text_height * 0.8);
    static const int center_x = (button_width - center_width) / 2;
    static const int right_x = center_x + center_width;
    static const int left_right_padding = button_padding;
    static const int left_right_width = button_width - right_x - left_right_padding;
    static const int heading_width = center_width * 8; // Estimate 6 + a few char safety margin for padding
    static const int heading_x = (button_width - heading_width) / 2;

    g_compass_left->setCoord(0 + left_right_padding, first_line_y, left_right_width, text_height);
    g_compass_center->setCoord(center_x, first_line_y, center_width, text_height);
    g_compass_right->setCoord(right_x, first_line_y, left_right_width, text_height);
    g_compass_caret->setCoord(center_x, caret_line_y, center_width, text_height);
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
        new_height = kCompassButtonHeightDouble;
        break;
    case CompassMode_NumberWithDirection:
        new_width = kCompassButtonWidthNumbers;
        break;
    case CompassMode_DirectionOnly:
        new_width = kCompassButtonWidthDirection;
        break;
    }
    g_compass_button->setRealSize(new_width, new_height);
    g_compass_button->setRealPosition((1.0F - new_width) / 2, kCompassButtonY);

    // Show/hide compass line widgets based on mode
    bool show_compass_line = (g_compact_mode == CompassMode_Full);
    g_compass_left->setVisible(show_compass_line);
    g_compass_center->setVisible(show_compass_line);
    g_compass_right->setVisible(show_compass_line);
    g_compass_caret->setVisible(show_compass_line);

    if (show_compass_line) { SetupCompassLineWidgets(); }

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
        //   Line 1: rotating tick labels via three tiled text widgets
        //   Line 2: caret pointing at the current value
        //   Line 3: exact yaw + direction
        float char_f = GetCenterCharIndex(yaw);
        int center_idx = static_cast<int>(std::floor(char_f + 0.5F));
        if (center_idx >= kFullLength) { center_idx -= kFullLength; }

        std::string left_str, center_str, right_str;
        BuildCompassSubstrings(center_idx, left_str, center_str, right_str);

        g_compass_left->setCaption(left_str);
        g_compass_center->setCaption(center_str);
        g_compass_right->setCaption(right_str);

        std::ostringstream num_oss;
        num_oss << "\n" << static_cast<int>(yaw) << "(" << DirectionName(yaw) << ")";
        caption = num_oss.str();
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
    float initial_x = (1.0F - kCompassButtonWidthNumbers) / 2;
    MyGUI::FloatCoord button_coord(initial_x, kCompassButtonY, kCompassButtonWidthNumbers, kCompassButtonHeightSingle);
    g_compass_button = gui->createWidgetReal<MyGUI::Button>(
        "Kenshi_Button1", button_coord, MyGUI::Align::Default, "Window", "CompassButton"
    );
    g_compass_button->setTextAlign(MyGUI::Align::Center);
    g_compass_button->setCaption("0(N)");
    g_compass_button->setVisible(false);
    g_compass_button->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);

    // Create compass line text widgets as children of the button.
    // Their coordinates are relative to the button, positioned by SetupCompassLineWidgets().
    g_compass_left = g_compass_button->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassLeft"
    );
    g_compass_left->setTextAlign(MyGUI::Align::Right);
    g_compass_left->setTextColour(GetCompassTextColour());
    g_compass_left->setVisible(false);
    g_compass_left->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);

    g_compass_center = g_compass_button->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassCenter"
    );
    g_compass_center->setTextAlign(MyGUI::Align::Center);
    g_compass_center->setTextColour(GetCompassTextColour());
    g_compass_center->setVisible(false);
    // FIXME: Zoom effect is not working as expected. Might just leave it as is
    g_compass_center->setFontHeight(static_cast<int>(g_compass_center->getFontHeight() * 1.5));
    g_compass_center->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);

    g_compass_right = g_compass_button->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassRight"
    );
    g_compass_right->setTextAlign(MyGUI::Align::Left);
    g_compass_right->setTextColour(GetCompassTextColour());
    g_compass_right->setVisible(false);
    g_compass_right->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);

    g_compass_caret = g_compass_button->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassCaret"
    );
    g_compass_caret->setTextAlign(MyGUI::Align::Center);
    g_compass_caret->setTextColour(MyGUI::Colour(1.0F, 0.0F, 0.0F)); // Red
    g_compass_caret->setCaption("^");
    g_compass_caret->setVisible(false);
    g_compass_caret->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);

    InitTickPositions();

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