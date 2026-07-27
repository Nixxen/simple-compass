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
MyGUI::Button *gCompassButton = nullptr;
MyGUI::TextBox *gCompassLeft = nullptr;
MyGUI::TextBox *gCompassCenter = nullptr;
MyGUI::TextBox *gCompassRight = nullptr;
MyGUI::TextBox *gCompassCaret = nullptr;
static const float kCompassButtonX = 0.5F;  // Centered horizontally
static const float kCompassButtonY = 0.01F; // Near the top of the screen
static const float kCompassButtonWidthFull = 0.20F;
static const float kCompassButtonWidthNumbers = 0.05F;
static const float kCompassButtonWidthDirection = kCompassButtonWidthNumbers * 0.5F;
static const float kCompassButtonHeightDouble = 0.04F;
static const float kCompassButtonHeightSingle = 0.02F;

static const MyGUI::Colour &GetCompassTextColour()
{
    static const MyGUI::Colour colour(0.4F, 0.380392F, 0.356863F); // Kenshi button normal text color
    return colour;
}

static const MyGUI::Colour &GetCompassTextHighlightColour()
{
    static const MyGUI::Colour colour(0.505882F, 0.4F, 0.207843F); // Kenshi button highlighted text color
    return colour;
}

static void SetCompassTextHighlighted(bool highlighted)
{
    const MyGUI::Colour &col = highlighted ? GetCompassTextHighlightColour() : GetCompassTextColour();
    gCompassButton->setTextColour(col);
    gCompassLeft->setTextColour(col);
    gCompassCenter->setTextColour(col);
    gCompassRight->setTextColour(col);
}

void OnCompassMouseSetFocus(MyGUI::WidgetPtr /*sender*/, MyGUI::WidgetPtr /*old*/) { SetCompassTextHighlighted(true); }

void OnCompassMouseLostFocus(MyGUI::WidgetPtr /*sender*/, MyGUI::WidgetPtr /*new_widget*/)
{ SetCompassTextHighlighted(false); }

enum CompassMode
{
    CompassMode_Full = 0, // Full compass: rotating ticks + caret + number
    CompassMode_NumberWithDirection,
    CompassMode_DirectionOnly,
    CompassMode_Count
};
CompassMode gCompactMode = CompassMode_NumberWithDirection;

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
static float gTickCharPositions[kTicksInCompass] = {0.0F};
static bool gTickPositionsInitialized = false;

// Initialises gTickCharPositions by finding each label in kFullString.
void InitTickPositions()
{
    if (gTickPositionsInitialized) { return; }
    int searchStart = 0;
    for (int i = 0; i < kTicksInCompass; ++i)
    {
        const char *found = strstr(kFullString + searchStart, kTickLabels[i]);
        if (found == nullptr) { continue; }
        int start = static_cast<int>(found - kFullString);
        int len = static_cast<int>(strlen(kTickLabels[i]));
        gTickCharPositions[i] = static_cast<float>(start) + (static_cast<float>(len - 1) * 0.5F);
        searchStart = start + len;
    }
    gTickPositionsInitialized = true;
}

// Returns yaw in degrees (0-360), 0 = North
float GetYawDegrees()
{
    if (au == nullptr || au->camera == nullptr) { return 0.0F; }
    Ogre::Vector3 facing = au->camera->getFacingDirection();
    float yawRad = std::atan2(facing.x, -facing.z); // -Z = North = 0
    float yawDeg = yawRad * 180.0F / boost::math::constants::pi<float>();
    if (yawDeg < 0.0F) { yawDeg += 360.0F; }
    return yawDeg;
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
float GetCenterCharIndex(float yawDeg)
{
    float tick = yawDeg / kDegreesPerTick;
    int tickLow = static_cast<int>(tick) % kTicksInCompass;
    int tickHigh = (tickLow + 1) % kTicksInCompass;
    float frac = tick - static_cast<float>(static_cast<int>(tick));

    float posLow = gTickCharPositions[tickLow];
    float posHigh = gTickCharPositions[tickHigh];

    if (tickHigh > tickLow) { return posLow + (frac * (posHigh - posLow)); }

    // Wrap around: tick 23 → tick 0
    float dist = static_cast<float>(kFullLength) - posLow + posHigh;
    float result = posLow + (frac * dist);
    if (result >= static_cast<float>(kFullLength)) { result -= static_cast<float>(kFullLength); }
    return result;
}

// Builds the three compass substrings for the tiled layout.
// centerIdx is the integer character index at the centre of the window.
// The center textbox gets exactly 1 character (the semantic character at the
// exact facing direction). Left and right each get kFullLength/2 characters
// collected outward from center, with left reversed for right-aligned display.
void BuildCompassSubstrings(int centerIdx, std::string &leftOut, std::string &centerOut, std::string &rightOut)
{
    centerOut.clear();
    centerOut.push_back(kFullString[centerIdx]);

    int halfLength = kFullLength / 2;

    // Left: walk backwards from centerIdx-1, collect halfLength chars, then reverse
    leftOut.clear();
    for (int i = 0; i < halfLength; ++i)
    {
        int idx = (centerIdx - 1 - i + kFullLength) % kFullLength;
        leftOut.push_back(kFullString[idx]);
    }
    std::reverse(leftOut.begin(), leftOut.end());

    // Right: walk forwards from centerIdx+1, collect halfLength chars
    rightOut.clear();
    for (int i = 0; i < halfLength; ++i)
    {
        int idx = (centerIdx + 1 + i) % kFullLength;
        rightOut.push_back(kFullString[idx]);
    }
}

// Creates or repositions the compass line text widgets to match the button's current size.
void SetupCompassLineWidgets()
{
    if (gCompassButton == nullptr || gCompassLeft == nullptr || gCompassCenter == nullptr || gCompassRight == nullptr ||
        gCompassCaret == nullptr)
    {
        ErrorLog("One or more compass  widgets are not initialized!");
        return;
    }

    static const int buttonWidth = gCompassButton->getWidth();
    static const int buttonHeight = gCompassButton->getHeight();

    static const int buttonPadding = 5; // Rough spacing from outside edge to inside button
    static const int textHeight = gCompassLeft->getFontHeight();
    static const int firstLineY = buttonPadding;
    // Almost overlap with scrolling text
    static const int caretLineY = firstLineY + static_cast<int>(textHeight * 0.7);
    // 1 character wide. Estimate width to be roughly equal to font height, minus padding
    static const int centerWidth = static_cast<int>(textHeight * 0.8);
    static const int centerX = (buttonWidth - centerWidth) / 2;
    static const int rightX = centerX + centerWidth;
    static const int leftRightWidth = buttonWidth - rightX - buttonPadding;

    gCompassLeft->setCoord(0 + buttonPadding, firstLineY, leftRightWidth, textHeight);
    gCompassCenter->setCoord(centerX, firstLineY, centerWidth, textHeight);
    gCompassRight->setCoord(rightX, firstLineY, leftRightWidth, textHeight);
    gCompassCaret->setCoord(centerX, caretLineY, centerWidth, textHeight);
}

// Click handler to cycle compact mode
void OnCompassClick(MyGUI::WidgetPtr sender)
{
    auto nextMode = (static_cast<int>(gCompactMode) + 1) % static_cast<int>(CompassMode_Count);
    gCompactMode = static_cast<CompassMode>(nextMode);
    float newWidth = kCompassButtonWidthNumbers;
    float newHeight = kCompassButtonHeightSingle;
    switch (gCompactMode)
    {
    case CompassMode_Full:
        newWidth = kCompassButtonWidthFull;
        newHeight = kCompassButtonHeightDouble;
        break;
    case CompassMode_NumberWithDirection:
        newWidth = kCompassButtonWidthNumbers;
        break;
    case CompassMode_DirectionOnly:
        newWidth = kCompassButtonWidthDirection;
        break;
    }
    gCompassButton->setRealSize(newWidth, newHeight);
    gCompassButton->setRealPosition((1.0F - newWidth) / 2, kCompassButtonY);

    // Show/hide compass line widgets based on mode
    bool showCompassLine = (gCompactMode == CompassMode_Full);
    gCompassLeft->setVisible(showCompassLine);
    gCompassCenter->setVisible(showCompassLine);
    gCompassRight->setVisible(showCompassLine);
    gCompassCaret->setVisible(showCompassLine);

    if (showCompassLine) { SetupCompassLineWidgets(); }

    std::string logMessage = std::string("Compass mode changed to ") + CompassModeName(gCompactMode);
    DebugLog(logMessage);
}

void UpdateCompass()
{
    if (gCompassButton == nullptr)
    {
        ErrorLog("Compass button not initialized!");
        return;
    }
    if (gCompassButton->getVisible() == false) { gCompassButton->setVisible(true); }

    float yaw = GetYawDegrees();
    std::string caption;

    if (gCompactMode == CompassMode_Full)
    {
        // Three-line layout:
        //   Line 1: rotating tick labels via three tiled text widgets
        //   Line 2: caret pointing at the current value
        //   Line 3: exact yaw + direction
        float centerCharIdx = GetCenterCharIndex(yaw);
        int centerIdx = static_cast<int>(std::floor(centerCharIdx + 0.5F));
        if (centerIdx >= kFullLength) { centerIdx -= kFullLength; }

        std::string leftStr;
        std::string centerStr;
        std::string rightStr;
        BuildCompassSubstrings(centerIdx, leftStr, centerStr, rightStr);

        gCompassLeft->setCaption(leftStr);
        gCompassCenter->setCaption(centerStr);
        gCompassRight->setCaption(rightStr);

        std::ostringstream numOss;
        numOss << "\n" << static_cast<int>(yaw) << "(" << DirectionName(yaw) << ")";
        caption = numOss.str();
    }
    else if (gCompactMode == CompassMode_NumberWithDirection)
    {
        std::ostringstream oss;
        oss << static_cast<int>(yaw) << "(" << DirectionName(yaw) << ")";
        caption = oss.str();
    }
    else if (gCompactMode == CompassMode_DirectionOnly) { caption = DirectionName(yaw); }
    else
    {
        caption = "Unknown Mode";
    }

    gCompassButton->setCaption(caption);
}

// Title screen constructor hook
TitleScreen *(*TitleScreen_orig)(TitleScreen *) = nullptr;
TitleScreen *TitleScreen_hook(TitleScreen *thisptr)
{
    TitleScreen *titleScreen = TitleScreen_orig(thisptr);

    MyGUI::Gui *gui = MyGUI::Gui::getInstancePtr();
    if (gui == nullptr)
    {
        ErrorLog("MyGUI::Gui instance not found!");
        return titleScreen;
    }

    // Clickable button to cycle compass mode
    float initialX = (1.0F - kCompassButtonWidthNumbers) / 2;
    MyGUI::FloatCoord button_coord(initialX, kCompassButtonY, kCompassButtonWidthNumbers, kCompassButtonHeightSingle);
    gCompassButton = gui->createWidgetReal<MyGUI::Button>(
        "Kenshi_Button1", button_coord, MyGUI::Align::Default, "Window", "CompassButton"
    );
    gCompassButton->setTextAlign(MyGUI::Align::Center);
    gCompassButton->setCaption("0(N)");
    gCompassButton->setVisible(false);
    gCompassButton->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);
    gCompassButton->eventMouseSetFocus += MyGUI::newDelegate(OnCompassMouseSetFocus);
    gCompassButton->eventMouseLostFocus += MyGUI::newDelegate(OnCompassMouseLostFocus);

    // Create compass line text widgets as children of the button.
    // Their coordinates are relative to the button, positioned by SetupCompassLineWidgets().
    gCompassLeft = gCompassButton->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassLeft"
    );
    gCompassLeft->setTextAlign(MyGUI::Align::Right);
    gCompassLeft->setTextColour(GetCompassTextColour());
    gCompassLeft->setVisible(false);
    gCompassLeft->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);
    gCompassLeft->eventMouseSetFocus += MyGUI::newDelegate(OnCompassMouseSetFocus);
    gCompassLeft->eventMouseLostFocus += MyGUI::newDelegate(OnCompassMouseLostFocus);

    gCompassCenter = gCompassButton->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassCenter"
    );
    gCompassCenter->setTextAlign(MyGUI::Align::Center);
    gCompassCenter->setTextColour(GetCompassTextColour());
    gCompassCenter->setVisible(false);
    // FIXME: Zoom effect is not working as expected. Might just leave it as is
    gCompassCenter->setFontHeight(static_cast<int>(gCompassCenter->getFontHeight() * 1.5));
    gCompassCenter->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);
    gCompassCenter->eventMouseSetFocus += MyGUI::newDelegate(OnCompassMouseSetFocus);
    gCompassCenter->eventMouseLostFocus += MyGUI::newDelegate(OnCompassMouseLostFocus);

    gCompassRight = gCompassButton->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassRight"
    );
    gCompassRight->setTextAlign(MyGUI::Align::Left);
    gCompassRight->setTextColour(GetCompassTextColour());
    gCompassRight->setVisible(false);
    gCompassRight->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);
    gCompassRight->eventMouseSetFocus += MyGUI::newDelegate(OnCompassMouseSetFocus);
    gCompassRight->eventMouseLostFocus += MyGUI::newDelegate(OnCompassMouseLostFocus);

    gCompassCaret = gCompassButton->createWidget<MyGUI::TextBox>(
        "Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "CompassCaret"
    );
    gCompassCaret->setTextAlign(MyGUI::Align::Center);
    gCompassCaret->setTextColour(MyGUI::Colour(1.0F, 0.0F, 0.0F)); // Red
    gCompassCaret->setCaption("^");
    gCompassCaret->setVisible(false);
    gCompassCaret->eventMouseButtonClick += MyGUI::newDelegate(OnCompassClick);
    gCompassCaret->eventMouseSetFocus += MyGUI::newDelegate(OnCompassMouseLostFocus);
    gCompassCaret->eventMouseLostFocus += MyGUI::newDelegate(OnCompassMouseLostFocus);

    InitTickPositions();

    return titleScreen;
}

// Main loop hook
void (*GameWorld_mainLoop_orig)(GameWorld *thisptr, float time);
void GameWorld_mainLoop_hook(GameWorld *thisptr, float time)
{
    UpdateCompass();
    GameWorld_mainLoop_orig(thisptr, time);
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
                                  GameWorld_mainLoop_hook, &GameWorld_mainLoop_orig
                              ))
    {
        ErrorLog("Could not add main loop hook!");
    }
}