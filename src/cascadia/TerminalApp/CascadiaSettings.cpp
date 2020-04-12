// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include <argb.h>
#include <conattrs.hpp>
#include <io.h>
#include <fcntl.h>
#include "CascadiaSettings.h"
#include "../../types/inc/utils.hpp"
#include "../../inc/DefaultSettings.h"
#include "AppLogic.h"
#include "Utils.h"
#include "LibraryResources.h"

#include "PowershellCoreProfileGenerator.h"
#include "WslDistroGenerator.h"
#include "AzureCloudShellGenerator.h"

using namespace winrt::Microsoft::Terminal::Settings;
using namespace ::TerminalApp;
using namespace winrt::Microsoft::Terminal::TerminalControl;
using namespace winrt::TerminalApp;
using namespace Microsoft::Console;

static constexpr std::wstring_view PACKAGED_PROFILE_ICON_PATH{ L"ms-appx:///ProfileIcons/" };

static constexpr std::wstring_view PACKAGED_PROFILE_ICON_EXTENSION{ L".png" };
static constexpr std::wstring_view DEFAULT_LINUX_ICON_GUID{ L"{9acb9455-ca41-5af7-950f-6bca1bc9722f}" };

// make sure this matches defaults.json.
static constexpr std::wstring_view DEFAULT_WINDOWS_POWERSHELL_GUID{ L"{61c54bbd-c2c6-5271-96e7-009a87ff44bf}" };

// Method Description:
// - Returns the settings currently in use by the entire Terminal application.
// Throws:
// - HR E_INVALIDARG if the app isn't up and running.
const CascadiaSettings& CascadiaSettings::GetCurrentAppSettings()
{
    auto appLogic{ ::winrt::TerminalApp::implementation::AppLogic::Current() };
    THROW_HR_IF_NULL(E_INVALIDARG, appLogic);
    return *(appLogic->GetSettings());
}

CascadiaSettings::CascadiaSettings() :
    CascadiaSettings(true)
{
}

// Constructor Description:
// - Creates a new settings object. If addDynamicProfiles is true, we'll
//   automatically add the built-in profile generators to our list of profile
//   generators. Set this to `false` for unit testing.
// Arguments:
// - addDynamicProfiles: if true, we'll add the built-in DPGs.
CascadiaSettings::CascadiaSettings(const bool addDynamicProfiles)
{
    if (addDynamicProfiles)
    {
        _profileGenerators.emplace_back(std::make_unique<PowershellCoreProfileGenerator>());
        _profileGenerators.emplace_back(std::make_unique<WslDistroGenerator>());
        _profileGenerators.emplace_back(std::make_unique<AzureCloudShellGenerator>());
    }
}

// Method Description:
// - Finds a GUID associated with the given profile name. If no profile matches
//      the profile name, returns a std::nullopt.
// Arguments:
// - profileName: the name of the profile's GUID to return.
// Return Value:
// - the GUID associated with the profile name.
std::optional<GUID> CascadiaSettings::FindGuid(const std::wstring_view profileName) const noexcept
{
    std::optional<GUID> profileGuid{};

    for (const auto& profile : _profiles)
    {
        if (profileName == profile.GetName())
        {
            try
            {
                profileGuid = profile.GetGuid();
            }
            CATCH_LOG();

            break;
        }
    }

    return profileGuid;
}

// Method Description:
// - Finds a profile that matches the given GUID. If there is no profile in this
//      settings object that matches, returns nullptr.
// Arguments:
// - profileGuid: the GUID of the profile to return.
// Return Value:
// - a non-ownership pointer to the profile matching the given guid, or nullptr
//      if there is no match.
const Profile* CascadiaSettings::FindProfile(GUID profileGuid) const noexcept
{
    for (auto& profile : _profiles)
    {
        try
        {
            if (profile.GetGuid() == profileGuid)
            {
                return &profile;
            }
        }
        CATCH_LOG();
    }
    return nullptr;
}

// Method Description:
// - Returns an iterable collection of all of our Profiles.
// Arguments:
// - <none>
// Return Value:
// - an iterable collection of all of our Profiles.
std::basic_string_view<Profile> CascadiaSettings::GetProfiles() const noexcept
{
    return { &_profiles[0], _profiles.size() };
}

// Method Description:
// - Returns the globally configured keybindings
// Arguments:
// - <none>
// Return Value:
// - the globally configured keybindings
AppKeyBindings CascadiaSettings::GetKeybindings() const noexcept
{
    return _globals.GetKeybindings();
}

// Method Description:
// - Get a reference to our global settings
// Arguments:
// - <none>
// Return Value:
// - a reference to our global settings
GlobalAppSettings& CascadiaSettings::GlobalSettings()
{
    return _globals;
}

// Method Description:
// - Gets our list of warnings we found during loading. These are things that we
//   knew were bad when we called `_ValidateSettings` last.
// Return Value:
// - a reference to our list of warnings.
std::vector<TerminalApp::SettingsLoadWarnings>& CascadiaSettings::GetWarnings()
{
    return _warnings;
}

// Method Description:
// - Attempts to validate this settings structure. If there are critical errors
//   found, they'll be thrown as a SettingsLoadError. Non-critical errors, such
//   as not finding the default profile, will only result in an error. We'll add
//   all these warnings to our list of warnings, and the application can chose
//   to display these to the user.
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_ValidateSettings()
{
    _warnings.clear();

    // Make sure to check that profiles exists at all first and foremost:
    _ValidateProfilesExist();

    // Verify all profiles actually had a GUID specified, otherwise generate a
    // GUID for them. Make sure to do this before de-duping profiles and
    // checking that the default profile is set.
    _ValidateProfilesHaveGuid();

    // Re-order profiles so that all profiles from the user's settings appear
    // before profiles that _weren't_ in the user profiles.
    _ReorderProfilesToMatchUserSettingsOrder();

    // Remove hidden profiles _after_ re-ordering. The re-ordering uses the raw
    // json, and will get confused if the profile isn't in the list.
    _RemoveHiddenProfiles();

    // Then do some validation on the profiles. The order of these does not
    // terribly matter.
    _ValidateNoDuplicateProfiles();
    _ValidateDefaultProfileExists();

    // Ensure that all the profile's color scheme names are
    // actually the names of schemes we've parsed. If the scheme doesn't exist,
    // just use the hardcoded defaults
    _ValidateAllSchemesExist();

    // Ensure all profile's with specified images resources have valid file path.
    // This validates icons and background images.
    _ValidateMediaResources();

    // TODO:GH#2548 ensure there's at least one key bound. Display a warning if
    // there's _NO_ keys bound to any actions. That's highly irregular, and
    // likely an indication of an error somehow.

    // GH#3522 - With variable args to keybindings, it's possible that a user
    // set a keybinding without all the required args for an action. Display a
    // warning if an action didn't have a required arg.
    // This will also catch other keybinding warnings, like from GH#4239
    _ValidateKeybindings();
}

// Method Description:
// - Checks if the settings contain profiles at all. As we'll need to have some
//   profiles at all, we'll throw an error if there aren't any profiles.
void CascadiaSettings::_ValidateProfilesExist()
{
    const bool hasProfiles = !_profiles.empty();
    if (!hasProfiles)
    {
        // Throw an exception. This is an invalid state, and we want the app to
        // be able to gracefully use the default settings.

        // We can't add the warning to the list of warnings here, because this
        // object is not going to be returned at any point.

        throw ::TerminalApp::SettingsException(::TerminalApp::SettingsLoadErrors::NoProfiles);
    }
}

// Method Description:
// - Walks through each profile, and ensures that they had a GUID set at some
//   point. If the profile did _not_ have a GUID ever set for it, generate a
//   temporary runtime GUID for it. This validation does not add any warnings.
void CascadiaSettings::_ValidateProfilesHaveGuid()
{
    for (auto& profile : _profiles)
    {
        profile.GenerateGuidIfNecessary();
    }
}

// Method Description:
// - Checks if the "defaultProfile" is set to one of the profiles we
//   actually have. If the value is unset, or the value is set to something that
//   doesn't exist in the list of profiles, we'll arbitrarily pick the first
//   profile to use temporarily as the default.
// - Appends a SettingsLoadWarnings::MissingDefaultProfile to our list of
//   warnings if we failed to find the default.
void CascadiaSettings::_ValidateDefaultProfileExists()
{
    const auto defaultProfileGuid = GlobalSettings().GetDefaultProfile();
    const bool nullDefaultProfile = defaultProfileGuid == GUID{};
    bool defaultProfileNotInProfiles = true;
    for (const auto& profile : _profiles)
    {
        if (profile.GetGuid() == defaultProfileGuid)
        {
            defaultProfileNotInProfiles = false;
            break;
        }
    }

    if (nullDefaultProfile || defaultProfileNotInProfiles)
    {
        _warnings.push_back(::TerminalApp::SettingsLoadWarnings::MissingDefaultProfile);
        // Use the first profile as the new default

        // _temporarily_ set the default profile to the first profile. Because
        // we're adding a warning, this settings change won't be re-serialized.
        GlobalSettings().SetDefaultProfile(_profiles[0].GetGuid());
    }
}

// Method Description:
// - Checks to make sure there aren't any duplicate profiles in the list of
//   profiles. If so, we'll remove the subsequent entries (temporarily), as they
//   won't be accessible anyways.
// - Appends a SettingsLoadWarnings::DuplicateProfile to our list of warnings if
//   we find any such duplicate.
void CascadiaSettings::_ValidateNoDuplicateProfiles()
{
    bool foundDupe = false;

    std::vector<size_t> indicesToDelete;

    std::set<GUID> uniqueGuids;

    // Try collecting all the unique guids. If we ever encounter a guid that's
    // already in the set, then we need to delete that profile.
    for (size_t i = 0; i < _profiles.size(); i++)
    {
        if (!uniqueGuids.insert(_profiles.at(i).GetGuid()).second)
        {
            foundDupe = true;
            indicesToDelete.push_back(i);
        }
    }

    // Remove all the duplicates we've marked
    // Walk backwards, so we don't accidentally shift any of the elements
    for (auto iter = indicesToDelete.rbegin(); iter != indicesToDelete.rend(); iter++)
    {
        _profiles.erase(_profiles.begin() + *iter);
    }

    if (foundDupe)
    {
        _warnings.push_back(::TerminalApp::SettingsLoadWarnings::DuplicateProfile);
    }
}

// Method Description:
// - Re-orders the list of profiles to match what the user would expect them to
//   be. Orders profiles to be in the ordering { [profiles from user settings],
//   [default profiles that weren't in the user profiles]}.
// - Does not set any warnings.
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_ReorderProfilesToMatchUserSettingsOrder()
{
    std::set<GUID> uniqueGuids;
    std::deque<GUID> guidOrder;

    auto collectGuids = [&](const auto& json) {
        for (auto profileJson : _GetProfilesJsonObject(json))
        {
            if (profileJson.isObject())
            {
                auto guid = Profile::GetGuidOrGenerateForJson(profileJson);
                if (uniqueGuids.insert(guid).second)
                {
                    guidOrder.push_back(guid);
                }
            }
        }
    };

    // Push all the userSettings profiles' GUIDS into the set
    collectGuids(_userSettings);

    // Push all the defaultSettings profiles' GUIDS into the set
    collectGuids(_defaultSettings);
    std::equal_to<GUID> equals;
    // Re-order the list of _profiles to match that ordering
    // for (gIndex=0 -> uniqueGuids.size)
    //   pIndex = the pIndex of the profile with guid==guids[gIndex]
    //   profiles.swap(pIndex <-> gIndex)
    // This is O(N^2), which is kinda rough. I'm sure there's a better way
    for (size_t gIndex = 0; gIndex < guidOrder.size(); gIndex++)
    {
        const auto guid = guidOrder.at(gIndex);
        for (size_t pIndex = gIndex; pIndex < _profiles.size(); pIndex++)
        {
            auto profileGuid = _profiles.at(pIndex).GetGuid();
            if (equals(profileGuid, guid))
            {
                std::iter_swap(_profiles.begin() + pIndex, _profiles.begin() + gIndex);
                break;
            }
        }
    }
}

// Method Description:
// - Removes any profiles marked "hidden" from the list of profiles.
// - Does not set any warnings.
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_RemoveHiddenProfiles()
{
    // remove_if will move all the profiles where the lambda is true to the end
    // of the list, then return a iterator to the point in the list where those
    // profiles start. The erase call will then remove all of those profiles
    // from the list. This is the [erase-remove
    // idiom](https://en.wikipedia.org/wiki/Erase%E2%80%93remove_idiom)
    _profiles.erase(std::remove_if(_profiles.begin(),
                                   _profiles.end(),
                                   [](auto&& profile) { return profile.IsHidden(); }),
                    _profiles.end());

    // Ensure that we still have some profiles here. If we don't, then throw an
    // exception, so the app can use the defaults.
    const bool hasProfiles = !_profiles.empty();
    if (!hasProfiles)
    {
        // Throw an exception. This is an invalid state, and we want the app to
        // be able to gracefully use the default settings.
        throw ::TerminalApp::SettingsException(::TerminalApp::SettingsLoadErrors::AllProfilesHidden);
    }
}

// Method Description:
// - Ensures that every profile has a valid "color scheme" set. If any profile
//   has a colorScheme set to a value which is _not_ the name of an actual color
//   scheme, we'll set the color table of the profile to something reasonable.
// Arguments:
// - <none>
// Return Value:
// - <none>
// - Appends a SettingsLoadWarnings::UnknownColorScheme to our list of warnings if
//   we find any such duplicate.
void CascadiaSettings::_ValidateAllSchemesExist()
{
    bool foundInvalidScheme = false;
    for (auto& profile : _profiles)
    {
        auto schemeName = profile.GetSchemeName();
        if (schemeName.has_value())
        {
            const auto found = _globals.GetColorSchemes().find(schemeName.value());
            if (found == _globals.GetColorSchemes().end())
            {
                profile.SetColorScheme({ L"Campbell" });
                foundInvalidScheme = true;
            }
        }
    }

    if (foundInvalidScheme)
    {
        _warnings.push_back(::TerminalApp::SettingsLoadWarnings::UnknownColorScheme);
    }
}

// Method Description:
// - Ensures that all specified images resources (icons and background images) are valid URIs.
//   This does not verify that the icon or background image files are encoded as an image.
// Arguments:
// - <none>
// Return Value:
// - <none>
// - Appends a SettingsLoadWarnings::InvalidBackgroundImage to our list of warnings if
//   we find any invalid background images.
// - Appends a SettingsLoadWarnings::InvalidIconImage to our list of warnings if
//   we find any invalid icon images.
void CascadiaSettings::_ValidateMediaResources()
{
    bool invalidBackground{ false };
    bool invalidIcon{ false };

    for (auto& profile : _profiles)
    {
        if (profile.HasBackgroundImage())
        {
            // Attempt to convert the path to a URI, the ctor will throw if it's invalid/unparseable.
            // This covers file paths on the machine, app data, URLs, and other resource paths.
            try
            {
                winrt::Windows::Foundation::Uri imagePath{ profile.GetExpandedBackgroundImagePath() };
            }
            catch (...)
            {
                profile.ResetBackgroundImagePath();
                invalidBackground = true;
            }
        }

        if (profile.HasIcon())
        {
            try
            {
                winrt::Windows::Foundation::Uri imagePath{ profile.GetExpandedIconPath() };
            }
            catch (...)
            {
                profile.ResetIconPath();
                invalidIcon = true;
            }
        }
    }

    if (invalidBackground)
    {
        _warnings.push_back(::TerminalApp::SettingsLoadWarnings::InvalidBackgroundImage);
    }

    if (invalidIcon)
    {
        _warnings.push_back(::TerminalApp::SettingsLoadWarnings::InvalidIcon);
    }
}

// Method Description:
// - Create a TerminalSettings object for the provided newTerminalArgs. We'll
//   use the newTerminalArgs to look up the profile that should be used to
//   create these TerminalSettings. Then, we'll apply settings contained in the
//   newTerminalArgs to the profile's settings, to enable customization on top
//   of the profile's default values.
// Arguments:
// - newTerminalArgs: An object that may contain a profile name or GUID to
//   actually use. If the Profile value is not a guid, we'll treat it as a name,
//   and attempt to look the profile up by name instead.
//   * Additionally, we'll use other values (such as Commandline,
//     StartingDirectory) in this object to override the settings directly from
//     the profile.
// Return Value:
// - the GUID of the created profile, and a fully initialized TerminalSettings object
std::tuple<GUID, TerminalSettings> CascadiaSettings::BuildSettings(const NewTerminalArgs& newTerminalArgs) const
{
    const GUID profileGuid = _GetProfileForArgs(newTerminalArgs);
    auto settings = BuildSettings(profileGuid);

    if (newTerminalArgs)
    {
        // Override commandline, starting directory if they exist in newTerminalArgs
        if (!newTerminalArgs.Commandline().empty())
        {
            settings.Commandline(newTerminalArgs.Commandline());
        }
        if (!newTerminalArgs.StartingDirectory().empty())
        {
            settings.StartingDirectory(newTerminalArgs.StartingDirectory());
        }
        if (!newTerminalArgs.TabTitle().empty())
        {
            settings.StartingTitle(newTerminalArgs.TabTitle());
        }
    }

    return { profileGuid, settings };
}

// Method Description:
// - Create a TerminalSettings object for the profile with a GUID matching the
//   provided GUID. If no profile matches this GUID, then this method will
//   throw.
// Arguments:
// - profileGuid: The GUID of a profile to use to create a settings object for.
// Return Value:
// - the GUID of the created profile, and a fully initialized TerminalSettings object
TerminalSettings CascadiaSettings::BuildSettings(GUID profileGuid) const
{
    const Profile* const profile = FindProfile(profileGuid);
    THROW_HR_IF_NULL(E_INVALIDARG, profile);

    TerminalSettings result = profile->CreateTerminalSettings(_globals.GetColorSchemes());

    // Place our appropriate global settings into the Terminal Settings
    _globals.ApplyToSettings(result);

    return result;
}

// Method Description:
// - Helper to get the GUID of a profile, given an optional index and a possible
//   "profile" value to override that.
// - First, we'll try looking up the profile for the given index. This will
//   either get us the GUID of the Nth profile, or the GUID of the default
//   profile.
// - Then, if there was a Profile set in the NewTerminalArgs, we'll use that to
//   try and look the profile up by either GUID or name.
// Arguments:
// - index: if provided, the index in the list of profiles to get the GUID for.
//   If omitted, instead use the default profile's GUID
// - newTerminalArgs: An object that may contain a profile name or GUID to
//   actually use. If the Profile value is not a guid, we'll treat it as a name,
//   and attempt to look the profile up by name instead.
// Return Value:
// - the GUID of the profile corresponding to this combination of index and NewTerminalArgs
GUID CascadiaSettings::_GetProfileForArgs(const NewTerminalArgs& newTerminalArgs) const
{
    std::optional<int> profileIndex{ std::nullopt };
    if (newTerminalArgs &&
        newTerminalArgs.ProfileIndex() != nullptr)
    {
        profileIndex = newTerminalArgs.ProfileIndex().Value();
    }
    GUID profileGuid = _GetProfileForIndex(profileIndex);

    if (newTerminalArgs)
    {
        const auto profileString = newTerminalArgs.Profile();

        // First, try and parse the "profile" argument as a GUID. If it's a
        // GUID, and the GUID of one of our profiles, then use that as the
        // profile GUID instead. If it's not, then try looking it up as a
        // name of a profile. If it's still not that, then just ignore it.
        if (!profileString.empty())
        {
            bool wasGuid = false;

            // Do a quick heuristic check - is the profile 38 chars long (the
            // length of a GUID string), and does it start with '{'? Because if
            // it doesn't, it's _definitely_ not a GUID.
            if (profileString.size() == 38 && profileString[0] == L'{')
            {
                try
                {
                    const auto newGUID = Utils::GuidFromString(profileString.c_str());
                    if (FindProfile(newGUID))
                    {
                        profileGuid = newGUID;
                        wasGuid = true;
                    }
                }
                CATCH_LOG();
            }

            // Here, we were unable to use the profile string as a GUID to
            // lookup a profile. Instead, try using the string to look the
            // Profile up by name.
            if (!wasGuid)
            {
                const auto guidFromName = FindGuid(profileString.c_str());
                if (guidFromName.has_value())
                {
                    profileGuid = guidFromName.value();
                }
            }
        }
    }

    return profileGuid;
}

// Method Description:
// - Helper to find the profile GUID for a the profile at the given index in the
//   list of profiles. If no index is provided, this instead returns the default
//   profile's guid. This is used by the NewTabProfile<N> ShortcutActions to
//   create a tab for the Nth profile in the list of profiles.
// Arguments:
// - index: if provided, the index in the list of profiles to get the GUID for.
//   If omitted, instead return the default profile's GUID
// Return Value:
// - the Nth profile's GUID, or the default profile's GUID
GUID CascadiaSettings::_GetProfileForIndex(std::optional<int> index) const
{
    GUID profileGuid;
    if (index)
    {
        const auto realIndex = index.value();
        // If we don't have that many profiles, then do nothing.
        if (realIndex < 0 ||
            realIndex >= gsl::narrow_cast<decltype(realIndex)>(_profiles.size()))
        {
            return _globals.GetDefaultProfile();
        }
        const auto& selectedProfile = _profiles.at(realIndex);
        profileGuid = selectedProfile.GetGuid();
    }
    else
    {
        // get Guid for the default profile
        profileGuid = _globals.GetDefaultProfile();
    }
    return profileGuid;
}

// Method Description:
// - If there were any warnings we generated while parsing the user's
//   keybindings, add them to the list of warnings here. If there were warnings
//   generated in this way, we'll add a AtLeastOneKeybindingWarning, which will
//   act as a header for the other warnings
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::_ValidateKeybindings()
{
    auto keybindingWarnings = _globals.GetKeybindingsWarnings();

    if (!keybindingWarnings.empty())
    {
        _warnings.push_back(::TerminalApp::SettingsLoadWarnings::AtLeastOneKeybindingWarning);
        _warnings.insert(_warnings.end(), keybindingWarnings.begin(), keybindingWarnings.end());
    }
}

// Method Description
// - Replaces known tokens DEFAULT_PROFILE, PRODUCT and VERSION in the settings template
//   with their expected values. DEFAULT_PROFILE is updated to match PowerShell Core's GUID
//   if such a profile is detected. If it isn't, it'll be set to Windows PowerShell's GUID.
// Arguments:
// - settingsTemplate: a settings template
// Return value:
// - The new settings string.
std::string CascadiaSettings::_ApplyFirstRunChangesToSettingsTemplate(std::string_view settingsTemplate) const
{
    std::string finalSettings{ settingsTemplate };
    auto replace{ [](std::string& haystack, std::string_view needle, std::string_view replacement) {
        auto pos{ std::string::npos };
        while ((pos = haystack.rfind(needle, pos)) != std::string::npos)
        {
            haystack.replace(pos, needle.size(), replacement);
        }
    } };

    std::wstring defaultProfileGuid{ DEFAULT_WINDOWS_POWERSHELL_GUID };
    if (const auto psCoreProfileGuid{ FindGuid(PowershellCoreProfileGenerator::GetPreferredPowershellProfileName()) })
    {
        defaultProfileGuid = Utils::GuidToString(*psCoreProfileGuid);
    }

    replace(finalSettings, "%DEFAULT_PROFILE%", til::u16u8(defaultProfileGuid));
    if (const auto appLogic{ winrt::TerminalApp::implementation::AppLogic::Current() })
    {
        replace(finalSettings, "%VERSION%", til::u16u8(appLogic->ApplicationVersion()));
        replace(finalSettings, "%PRODUCT%", til::u16u8(appLogic->ApplicationDisplayName()));
    }

    replace(finalSettings, "%COMMAND_PROMPT_LOCALIZED_NAME%", RS_A(L"CommandPromptDisplayName"));

    return finalSettings;
}
