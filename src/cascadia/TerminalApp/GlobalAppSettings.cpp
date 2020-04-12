// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "GlobalAppSettings.h"
#include "../../types/inc/Utils.hpp"
#include "../../inc/DefaultSettings.h"
#include "Utils.h"
#include "JsonUtils.h"
#include <sstream>

using namespace TerminalApp;
using namespace winrt::Microsoft::Terminal::Settings;
using namespace winrt::TerminalApp;
using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::UI::Xaml;
using namespace ::Microsoft::Console;
using namespace winrt::Microsoft::UI::Xaml::Controls;

static constexpr std::string_view KeybindingsKey{ "keybindings" };
static constexpr std::string_view DefaultProfileKey{ "defaultProfile" };
static constexpr std::string_view AlwaysShowTabsKey{ "alwaysShowTabs" };
static constexpr std::string_view InitialRowsKey{ "initialRows" };
static constexpr std::string_view InitialColsKey{ "initialCols" };
static constexpr std::string_view RowsToScrollKey{ "rowsToScroll" };
static constexpr std::string_view InitialPositionKey{ "initialPosition" };
static constexpr std::string_view ShowTitleInTitlebarKey{ "showTerminalTitleInTitlebar" };
static constexpr std::string_view ThemeKey{ "theme" };
static constexpr std::string_view TabWidthModeKey{ "tabWidthMode" };
static constexpr std::wstring_view EqualTabWidthModeValue{ L"equal" };
static constexpr std::wstring_view TitleLengthTabWidthModeValue{ L"titleLength" };
static constexpr std::string_view ShowTabsInTitlebarKey{ "showTabsInTitlebar" };
static constexpr std::string_view WordDelimitersKey{ "wordDelimiters" };
static constexpr std::string_view CopyOnSelectKey{ "copyOnSelect" };
static constexpr std::string_view CopyFormattingKey{ "copyFormatting" };
static constexpr std::string_view LaunchModeKey{ "launchMode" };
static constexpr std::string_view ConfirmCloseAllKey{ "confirmCloseAllTabs" };
static constexpr std::string_view SnapToGridOnResizeKey{ "snapToGridOnResize" };
static constexpr std::wstring_view DefaultLaunchModeValue{ L"default" };
static constexpr std::wstring_view MaximizedLaunchModeValue{ L"maximized" };
static constexpr std::wstring_view LightThemeValue{ L"light" };
static constexpr std::wstring_view DarkThemeValue{ L"dark" };
static constexpr std::wstring_view SystemThemeValue{ L"system" };

static constexpr std::string_view DebugFeaturesKey{ "debugFeatures" };

#ifdef _DEBUG
static constexpr bool debugFeaturesDefault{ true };
#else
static constexpr bool debugFeaturesDefault{ false };
#endif

GlobalAppSettings::GlobalAppSettings() :
    _keybindings{ winrt::make_self<winrt::TerminalApp::implementation::AppKeyBindings>() },
    _keybindingsWarnings{},
    _colorSchemes{},
    _defaultProfile{},
    _alwaysShowTabs{ true },
    _confirmCloseAllTabs{ true },
    _initialRows{ DEFAULT_ROWS },
    _initialCols{ DEFAULT_COLS },
    _rowsToScroll{ DEFAULT_ROWSTOSCROLL },
    _initialX{},
    _initialY{},
    _showTitleInTitlebar{ true },
    _showTabsInTitlebar{ true },
    _theme{ ElementTheme::Default },
    _tabWidthMode{ TabViewWidthMode::Equal },
    _wordDelimiters{ DEFAULT_WORD_DELIMITERS },
    _copyOnSelect{ false },
    _copyFormatting{ false },
    _launchMode{ LaunchMode::DefaultMode },
    _debugFeatures{ debugFeaturesDefault }
{
}

GlobalAppSettings::~GlobalAppSettings()
{
}

std::unordered_map<std::wstring, ColorScheme>& GlobalAppSettings::GetColorSchemes() noexcept
{
    return _colorSchemes;
}

const std::unordered_map<std::wstring, ColorScheme>& GlobalAppSettings::GetColorSchemes() const noexcept
{
    return _colorSchemes;
}

void GlobalAppSettings::SetDefaultProfile(const GUID defaultProfile) noexcept
{
    _defaultProfile = defaultProfile;
}

GUID GlobalAppSettings::GetDefaultProfile() const noexcept
{
    return _defaultProfile;
}

AppKeyBindings GlobalAppSettings::GetKeybindings() const noexcept
{
    return *_keybindings;
}

bool GlobalAppSettings::GetAlwaysShowTabs() const noexcept
{
    return _alwaysShowTabs;
}

void GlobalAppSettings::SetAlwaysShowTabs(const bool showTabs) noexcept
{
    _alwaysShowTabs = showTabs;
}

bool GlobalAppSettings::GetShowTitleInTitlebar() const noexcept
{
    return _showTitleInTitlebar;
}

void GlobalAppSettings::SetShowTitleInTitlebar(const bool showTitleInTitlebar) noexcept
{
    _showTitleInTitlebar = showTitleInTitlebar;
}

ElementTheme GlobalAppSettings::GetTheme() const noexcept
{
    return _theme;
}

void GlobalAppSettings::SetTheme(const ElementTheme theme) noexcept
{
    _theme = theme;
}

TabViewWidthMode GlobalAppSettings::GetTabWidthMode() const noexcept
{
    return _tabWidthMode;
}

void GlobalAppSettings::SetTabWidthMode(const TabViewWidthMode tabWidthMode)
{
    _tabWidthMode = tabWidthMode;
}

std::wstring GlobalAppSettings::GetWordDelimiters() const noexcept
{
    return _wordDelimiters;
}

void GlobalAppSettings::SetWordDelimiters(const std::wstring wordDelimiters) noexcept
{
    _wordDelimiters = wordDelimiters;
}

bool GlobalAppSettings::GetCopyOnSelect() const noexcept
{
    return _copyOnSelect;
}

void GlobalAppSettings::SetCopyOnSelect(const bool copyOnSelect) noexcept
{
    _copyOnSelect = copyOnSelect;
}

bool GlobalAppSettings::GetCopyFormatting() const noexcept
{
    return _copyFormatting;
}

LaunchMode GlobalAppSettings::GetLaunchMode() const noexcept
{
    return _launchMode;
}

void GlobalAppSettings::SetLaunchMode(const LaunchMode launchMode)
{
    _launchMode = launchMode;
}
bool GlobalAppSettings::GetConfirmCloseAllTabs() const noexcept
{
    return _confirmCloseAllTabs;
}

void GlobalAppSettings::SetConfirmCloseAllTabs(const bool confirmCloseAllTabs) noexcept
{
    _confirmCloseAllTabs = confirmCloseAllTabs;
}

bool GlobalAppSettings::DebugFeaturesEnabled() const noexcept
{
    return _debugFeatures;
}

#pragma region ExperimentalSettings
bool GlobalAppSettings::GetShowTabsInTitlebar() const noexcept
{
    return _showTabsInTitlebar;
}

void GlobalAppSettings::SetShowTabsInTitlebar(const bool showTabsInTitlebar) noexcept
{
    _showTabsInTitlebar = showTabsInTitlebar;
}

std::optional<int32_t> GlobalAppSettings::GetInitialX() const noexcept
{
    return _initialX;
}

std::optional<int32_t> GlobalAppSettings::GetInitialY() const noexcept
{
    return _initialY;
}

#pragma endregion

// Method Description:
// - Applies appropriate settings from the globals into the given TerminalSettings.
// Arguments:
// - settings: a TerminalSettings object to add global property values to.
// Return Value:
// - <none>
void GlobalAppSettings::ApplyToSettings(TerminalSettings& settings) const noexcept
{
    settings.KeyBindings(GetKeybindings());
    settings.InitialRows(_initialRows);
    settings.InitialCols(_initialCols);
    settings.RowsToScroll(_rowsToScroll);

    settings.WordDelimiters(_wordDelimiters);
    settings.CopyOnSelect(_copyOnSelect);
}

// Method Description:
// - Serialize this object to a JsonObject.
// Arguments:
// - <none>
// Return Value:
// - a JsonObject which is an equivalent serialization of this object.
Json::Value GlobalAppSettings::ToJson() const
{
    Json::Value jsonObject;

    jsonObject[JsonKey(DefaultProfileKey)] = winrt::to_string(Utils::GuidToString(_defaultProfile));
    jsonObject[JsonKey(InitialRowsKey)] = _initialRows;
    jsonObject[JsonKey(InitialColsKey)] = _initialCols;
    jsonObject[JsonKey(RowsToScrollKey)] = _rowsToScroll;
    jsonObject[JsonKey(InitialPositionKey)] = _SerializeInitialPosition(_initialX, _initialY);
    jsonObject[JsonKey(AlwaysShowTabsKey)] = _alwaysShowTabs;
    jsonObject[JsonKey(ShowTitleInTitlebarKey)] = _showTitleInTitlebar;
    jsonObject[JsonKey(ShowTabsInTitlebarKey)] = _showTabsInTitlebar;
    jsonObject[JsonKey(WordDelimitersKey)] = winrt::to_string(_wordDelimiters);
    jsonObject[JsonKey(CopyOnSelectKey)] = _copyOnSelect;
    jsonObject[JsonKey(CopyFormattingKey)] = _copyFormatting;
    jsonObject[JsonKey(LaunchModeKey)] = winrt::to_string(_SerializeLaunchMode(_launchMode));
    jsonObject[JsonKey(ThemeKey)] = winrt::to_string(_SerializeTheme(_theme));
    jsonObject[JsonKey(TabWidthModeKey)] = winrt::to_string(_SerializeTabWidthMode(_tabWidthMode));
    jsonObject[JsonKey(KeybindingsKey)] = _keybindings->ToJson();
    jsonObject[JsonKey(ConfirmCloseAllKey)] = _confirmCloseAllTabs;
    jsonObject[JsonKey(SnapToGridOnResizeKey)] = _SnapToGridOnResize;
    jsonObject[JsonKey(DebugFeaturesKey)] = _debugFeatures;

    return jsonObject;
}

// Method Description:
// - Create a new instance of this class from a serialized JsonObject.
// Arguments:
// - json: an object which should be a serialization of a GlobalAppSettings object.
// Return Value:
// - a new GlobalAppSettings instance created from the values in `json`
GlobalAppSettings GlobalAppSettings::FromJson(const Json::Value& json)
{
    GlobalAppSettings result;
    result.LayerJson(json);
    return result;
}

void GlobalAppSettings::LayerJson(const Json::Value& json)
{
    if (auto defaultProfile{ json[JsonKey(DefaultProfileKey)] })
    {
        auto guid = Utils::GuidFromString(GetWstringFromJson(defaultProfile));
        _defaultProfile = guid;
    }

    JsonUtils::GetBool(json, AlwaysShowTabsKey, _alwaysShowTabs);

    JsonUtils::GetBool(json, ConfirmCloseAllKey, _confirmCloseAllTabs);

    JsonUtils::GetInt(json, InitialRowsKey, _initialRows);

    JsonUtils::GetInt(json, InitialColsKey, _initialCols);

    if (auto rowsToScroll{ json[JsonKey(RowsToScrollKey)] })
    {
        //if it's not an int we fall back to setting it to 0, which implies using the system setting. This will be the case if it's set to "system"
        if (rowsToScroll.isInt())
        {
            _rowsToScroll = rowsToScroll.asInt();
        }
        else
        {
            _rowsToScroll = 0;
        }
    }

    if (auto initialPosition{ json[JsonKey(InitialPositionKey)] })
    {
        _ParseInitialPosition(GetWstringFromJson(initialPosition), _initialX, _initialY);
    }

    JsonUtils::GetBool(json, ShowTitleInTitlebarKey, _showTitleInTitlebar);

    JsonUtils::GetBool(json, ShowTabsInTitlebarKey, _showTabsInTitlebar);

    JsonUtils::GetWstring(json, WordDelimitersKey, _wordDelimiters);

    JsonUtils::GetBool(json, CopyOnSelectKey, _copyOnSelect);

    JsonUtils::GetBool(json, CopyFormattingKey, _copyFormatting);

    if (auto launchMode{ json[JsonKey(LaunchModeKey)] })
    {
        _launchMode = _ParseLaunchMode(GetWstringFromJson(launchMode));
    }

    if (auto theme{ json[JsonKey(ThemeKey)] })
    {
        _theme = _ParseTheme(GetWstringFromJson(theme));
    }

    if (auto tabWidthMode{ json[JsonKey(TabWidthModeKey)] })
    {
        _tabWidthMode = _ParseTabWidthMode(GetWstringFromJson(tabWidthMode));
    }

    if (auto keybindings{ json[JsonKey(KeybindingsKey)] })
    {
        auto warnings = _keybindings->LayerJson(keybindings);
        // It's possible that the user provided keybindings have some warnings
        // in them - problems that we should alert the user to, but we can
        // recover from. Most of these warnings cannot be detected later in the
        // Validate settings phase, so we'll collect them now. If there were any
        // warnings generated from parsing these keybindings, add them to our
        // list of warnings.
        _keybindingsWarnings.insert(_keybindingsWarnings.end(), warnings.begin(), warnings.end());
    }

    JsonUtils::GetBool(json, SnapToGridOnResizeKey, _SnapToGridOnResize);

    // GetBool will only override the current value if the key exists
    JsonUtils::GetBool(json, DebugFeaturesKey, _debugFeatures);
}

// Method Description:
// - Helper function for converting a user-specified cursor style corresponding
//   CursorStyle enum value
// Arguments:
// - themeString: The string value from the settings file to parse
// Return Value:
// - The corresponding enum value which maps to the string provided by the user
ElementTheme GlobalAppSettings::_ParseTheme(const std::wstring& themeString) noexcept
{
    if (themeString == LightThemeValue)
    {
        return ElementTheme::Light;
    }
    else if (themeString == DarkThemeValue)
    {
        return ElementTheme::Dark;
    }
    // default behavior for invalid data or SystemThemeValue
    return ElementTheme::Default;
}

// Method Description:
// - Helper function for converting a CursorStyle to its corresponding string
//   value.
// Arguments:
// - theme: The enum value to convert to a string.
// Return Value:
// - The string value for the given CursorStyle
std::wstring_view GlobalAppSettings::_SerializeTheme(const ElementTheme theme) noexcept
{
    switch (theme)
    {
    case ElementTheme::Light:
        return LightThemeValue;
    case ElementTheme::Dark:
        return DarkThemeValue;
    default:
        return SystemThemeValue;
    }
}

// Method Description:
// - Helper function for converting the initial position string into
//   2 coordinate values. We allow users to only provide one coordinate,
//   thus, we use comma as the separator:
//   (100, 100): standard input string
//   (, 100), (100, ): if a value is missing, we set this value as a default
//   (,): both x and y are set to default
//   (abc, 100): if a value is not valid, we treat it as default
//   (100, 100, 100): we only read the first two values, this is equivalent to (100, 100)
// Arguments:
// - initialPosition: the initial position string from json
//   initialX: reference to the _initialX member
//   initialY: reference to the _initialY member
// Return Value:
// - None
void GlobalAppSettings::_ParseInitialPosition(const std::wstring& initialPosition,
                                              std::optional<int32_t>& initialX,
                                              std::optional<int32_t>& initialY) noexcept
{
    const wchar_t singleCharDelim = L',';
    std::wstringstream tokenStream(initialPosition);
    std::wstring token;
    uint8_t initialPosIndex = 0;

    // Get initial position values till we run out of delimiter separated values in the stream
    // or we hit max number of allowable values (= 2)
    // Non-numeral values or empty string will be caught as exception and we do not assign them
    for (; std::getline(tokenStream, token, singleCharDelim) && (initialPosIndex < 2); initialPosIndex++)
    {
        try
        {
            int32_t position = std::stoi(token);
            if (initialPosIndex == 0)
            {
                initialX.emplace(position);
            }

            if (initialPosIndex == 1)
            {
                initialY.emplace(position);
            }
        }
        catch (...)
        {
            // Do nothing
        }
    }
}

// Method Description:
// - Helper function for converting X/Y initial positions to a string
//   value.
// Arguments:
// - initialX: reference to the _initialX member
//   initialY: reference to the _initialY member
// Return Value:
// - The concatenated string for the the current initialX and initialY
std::string GlobalAppSettings::_SerializeInitialPosition(const std::optional<int32_t>& initialX,
                                                         const std::optional<int32_t>& initialY) noexcept
{
    std::string serializedInitialPos = "";
    if (initialX.has_value())
    {
        serializedInitialPos += std::to_string(initialX.value());
    }

    serializedInitialPos += ", ";

    if (initialY.has_value())
    {
        serializedInitialPos += std::to_string(initialY.value());
    }

    return serializedInitialPos;
}

// Method Description:
// - Helper function for converting the user-specified launch mode
//   to a LaunchMode enum value
// Arguments:
// - launchModeString: The string value from the settings file to parse
// Return Value:
// - The corresponding enum value which maps to the string provided by the user
LaunchMode GlobalAppSettings::_ParseLaunchMode(const std::wstring& launchModeString) noexcept
{
    if (launchModeString == MaximizedLaunchModeValue)
    {
        return LaunchMode::MaximizedMode;
    }

    return LaunchMode::DefaultMode;
}

// Method Description:
// - Helper function for converting a LaunchMode to its corresponding string
//   value.
// Arguments:
// - launchMode: The enum value to convert to a string.
// Return Value:
// - The string value for the given LaunchMode
std::wstring_view GlobalAppSettings::_SerializeLaunchMode(const LaunchMode launchMode) noexcept
{
    switch (launchMode)
    {
    case LaunchMode::MaximizedMode:
        return MaximizedLaunchModeValue;
    default:
        return DefaultLaunchModeValue;
    }
}

// Method Description:
// - Helper function for converting the user-specified tab width
//   to a TabViewWidthMode enum value
// Arguments:
// - tabWidthModeString: The string value from the settings file to parse
// Return Value:
// - The corresponding enum value which maps to the string provided by the user
TabViewWidthMode GlobalAppSettings::_ParseTabWidthMode(const std::wstring& tabWidthModeString) noexcept
{
    if (tabWidthModeString == TitleLengthTabWidthModeValue)
    {
        return TabViewWidthMode::SizeToContent;
    }
    // default behavior for invalid data or EqualTabWidthValue
    return TabViewWidthMode::Equal;
}

// Method Description:
// - Helper function for converting a TabViewWidthMode to its corresponding string
//   value.
// Arguments:
// - tabWidthMode: The enum value to convert to a string.
// Return Value:
// - The string value for the given TabWidthMode
std::wstring_view GlobalAppSettings::_SerializeTabWidthMode(const TabViewWidthMode tabWidthMode) noexcept
{
    switch (tabWidthMode)
    {
    case TabViewWidthMode::SizeToContent:
        return TitleLengthTabWidthModeValue;
    default:
        return EqualTabWidthModeValue;
    }
}

// Method Description:
// - Adds the given colorscheme to our map of schemes, using its name as the key.
// Arguments:
// - scheme: the color scheme to add
// Return Value:
// - <none>
void GlobalAppSettings::AddColorScheme(ColorScheme scheme)
{
    std::wstring name{ scheme.GetName() };
    _colorSchemes[name] = std::move(scheme);
}

// Method Description:
// - Return the warnings that we've collected during parsing the JSON for the
//   keybindings. It's possible that the user provided keybindings have some
//   warnings in them - problems that we should alert the user to, but we can
//   recover from.
// Arguments:
// - <none>
// Return Value:
// - <none>
std::vector<TerminalApp::SettingsLoadWarnings> GlobalAppSettings::GetKeybindingsWarnings() const
{
    return _keybindingsWarnings;
}
