// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"

#include "../TerminalApp/ColorScheme.h"
#include "../TerminalApp/CascadiaSettings.h"

using namespace Microsoft::Console;
using namespace TerminalApp;
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;

namespace TerminalAppLocalTests
{
    // Unfortunately, these tests _WILL NOT_ work in our CI, until we have a lab
    // machine available that can run Windows version 18362.

    class SettingsTests
    {
        // Use a custom manifest to ensure that we can activate winrt types from
        // our test. This property will tell taef to manually use this as the
        // sxs manifest during this test class. It includes all the cppwinrt
        // types we've defined, so if your test is crashing for an unknown
        // reason, make sure it's included in that file.
        // If you want to do anything XAML-y, you'll need to run yor test in a
        // packaged context. See TabTests.cpp for more details on that.
        BEGIN_TEST_CLASS(SettingsTests)
            TEST_CLASS_PROPERTY(L"ActivationContext", L"TerminalApp.LocalTests.manifest")
        END_TEST_CLASS()

        TEST_METHOD(TryCreateWinRTType);
        TEST_METHOD(ValidateProfilesExist);
        TEST_METHOD(ValidateDefaultProfileExists);
        TEST_METHOD(ValidateDuplicateProfiles);
        TEST_METHOD(ValidateManyWarnings);

        TEST_CLASS_SETUP(ClassSetup)
        {
            reader = std::unique_ptr<Json::CharReader>(Json::CharReaderBuilder::CharReaderBuilder().newCharReader());
            return true;
        }
        Json::Value VerifyParseSucceeded(std::string content);

    private:
        std::unique_ptr<Json::CharReader> reader;
    };

    Json::Value SettingsTests::VerifyParseSucceeded(std::string content)
    {
        Json::Value root;
        std::string errs;
        const bool parseResult = reader->parse(content.c_str(), content.c_str() + content.size(), &root, &errs);
        VERIFY_IS_TRUE(parseResult, winrt::to_hstring(errs).c_str());
        return root;
    }

    void SettingsTests::TryCreateWinRTType()
    {
        winrt::Microsoft::Terminal::Settings::TerminalSettings settings{};
        VERIFY_IS_NOT_NULL(settings);
        auto oldFontSize = settings.FontSize();
        settings.FontSize(oldFontSize + 5);
        auto newFontSize = settings.FontSize();
        VERIFY_ARE_NOT_EQUAL(oldFontSize, newFontSize);
    }

    void SettingsTests::ValidateProfilesExist()
    {
        const std::string settingsWithProfiles{ R"(
        {
            "profiles": [
                {
                    "name" : "profile0"
                }
            ]
        })" };

        const std::string settingsWithoutProfiles{ R"(
        {
            "defaultProfile": "{6239a42c-1de4-49a3-80bd-e8fdd045185c}"
        })" };

        const std::string settingsWithEmptyProfiles{ R"(
        {
            "profiles": []
        })" };

        {
            // Case 1: Good settings
            const auto settingsObject = VerifyParseSucceeded(settingsWithProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            settings->_ValidateProfilesExist();
        }
        {
            // Case 2: Bad settings
            const auto settingsObject = VerifyParseSucceeded(settingsWithoutProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            bool caughtExpectedException = false;
            try
            {
                settings->_ValidateProfilesExist();
            }
            catch (const ::TerminalApp::SettingsException& ex)
            {
                VERIFY_IS_TRUE(ex.Error() == ::TerminalApp::SettingsLoadErrors::NoProfiles);
                caughtExpectedException = true;
            }
            VERIFY_IS_TRUE(caughtExpectedException);
        }
        {
            // Case 3: Bad settings
            const auto settingsObject = VerifyParseSucceeded(settingsWithEmptyProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            bool caughtExpectedException = false;
            try
            {
                settings->_ValidateProfilesExist();
            }
            catch (const ::TerminalApp::SettingsException& ex)
            {
                VERIFY_IS_TRUE(ex.Error() == ::TerminalApp::SettingsLoadErrors::NoProfiles);
                caughtExpectedException = true;
            }
            VERIFY_IS_TRUE(caughtExpectedException);
        }
    }

    void SettingsTests::ValidateDefaultProfileExists()
    {
        const std::string goodProfiles{ R"(
        {
            "globals": {
                "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}"
            },
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        const std::string badProfiles{ R"(
        {
            "globals": {
                "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}"
            },
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-3333-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-4444-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        const std::string noDefaultAtAll{ R"(
        {
            "globals": {
                "alwaysShowTabs": true
            },
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-5555-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-6666-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        {
            // Case 1: Good settings
            Log::Comment(NoThrowString().Format(
                L"Testing a pair of profiles with unique guids, and the defaultProfile is one of those guids"));
            const auto settingsObject = VerifyParseSucceeded(goodProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            settings->_ValidateDefaultProfileExists();
            VERIFY_ARE_EQUAL(static_cast<size_t>(0), settings->_warnings.size());
            VERIFY_ARE_EQUAL(static_cast<size_t>(2), settings->_profiles.size());
            VERIFY_ARE_EQUAL(settings->_globals.GetDefaultProfile(), settings->_profiles.at(0).GetGuid());
        }
        {
            // Case 2: Bad settings
            Log::Comment(NoThrowString().Format(
                L"Testing a pair of profiles with unique guids, but the defaultProfile is NOT one of those guids"));
            const auto settingsObject = VerifyParseSucceeded(badProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            settings->_ValidateDefaultProfileExists();
            VERIFY_ARE_EQUAL(static_cast<size_t>(1), settings->_warnings.size());
            VERIFY_ARE_EQUAL(::TerminalApp::SettingsLoadWarnings::MissingDefaultProfile, settings->_warnings.at(0));

            VERIFY_ARE_EQUAL(static_cast<size_t>(2), settings->_profiles.size());
            VERIFY_ARE_EQUAL(settings->_globals.GetDefaultProfile(), settings->_profiles.at(0).GetGuid());
        }
        {
            // Case 2: Bad settings
            Log::Comment(NoThrowString().Format(
                L"Testing a pair of profiles with unique guids, and no defaultProfile at all"));
            const auto settingsObject = VerifyParseSucceeded(badProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            settings->_ValidateDefaultProfileExists();
            VERIFY_ARE_EQUAL(static_cast<size_t>(1), settings->_warnings.size());
            VERIFY_ARE_EQUAL(::TerminalApp::SettingsLoadWarnings::MissingDefaultProfile, settings->_warnings.at(0));

            VERIFY_ARE_EQUAL(static_cast<size_t>(2), settings->_profiles.size());
            VERIFY_ARE_EQUAL(settings->_globals.GetDefaultProfile(), settings->_profiles.at(0).GetGuid());
        }
    }

    void SettingsTests::ValidateDuplicateProfiles()
    {
        const std::string goodProfiles{ R"(
        {
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        const std::string badProfiles{ R"(
        {
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-3333-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-3333-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        const std::string veryBadProfiles{ R"(
        {
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-4444-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-5555-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile2",
                    "guid": "{6239a42c-4444-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile3",
                    "guid": "{6239a42c-4444-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile4",
                    "guid": "{6239a42c-6666-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile5",
                    "guid": "{6239a42c-5555-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile6",
                    "guid": "{6239a42c-7777-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        {
            // Case 1: Good settings
            Log::Comment(NoThrowString().Format(
                L"Testing a pair of profiles with unique guids"));
            const auto settingsObject = VerifyParseSucceeded(goodProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            settings->_ValidateNoDuplicateProfiles();
            VERIFY_ARE_EQUAL(static_cast<size_t>(0), settings->_warnings.size());
            VERIFY_ARE_EQUAL(static_cast<size_t>(2), settings->_profiles.size());
        }
        {
            // Case 2: Bad settings
            Log::Comment(NoThrowString().Format(
                L"Testing a pair of profiles with the same guid"));
            const auto settingsObject = VerifyParseSucceeded(badProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);

            settings->_ValidateNoDuplicateProfiles();

            VERIFY_ARE_EQUAL(static_cast<size_t>(1), settings->_warnings.size());
            VERIFY_ARE_EQUAL(::TerminalApp::SettingsLoadWarnings::DuplicateProfile, settings->_warnings.at(0));

            VERIFY_ARE_EQUAL(static_cast<size_t>(1), settings->_profiles.size());
            VERIFY_ARE_EQUAL(L"profile0", settings->_profiles.at(0).GetName());
        }
        {
            // Case 3: Very bad settings
            Log::Comment(NoThrowString().Format(
                L"Testing a set of profiles, many of which with duplicated guids"));
            const auto settingsObject = VerifyParseSucceeded(veryBadProfiles);
            auto settings = CascadiaSettings::FromJson(settingsObject);
            settings->_ValidateNoDuplicateProfiles();
            VERIFY_ARE_EQUAL(static_cast<size_t>(1), settings->_warnings.size());
            VERIFY_ARE_EQUAL(::TerminalApp::SettingsLoadWarnings::DuplicateProfile, settings->_warnings.at(0));

            VERIFY_ARE_EQUAL(static_cast<size_t>(4), settings->_profiles.size());
            VERIFY_ARE_EQUAL(L"profile0", settings->_profiles.at(0).GetName());
            VERIFY_ARE_EQUAL(L"profile1", settings->_profiles.at(1).GetName());
            VERIFY_ARE_EQUAL(L"profile4", settings->_profiles.at(2).GetName());
            VERIFY_ARE_EQUAL(L"profile6", settings->_profiles.at(3).GetName());
        }
    }

    void SettingsTests::ValidateManyWarnings()
    {
        const std::string badProfiles{ R"(
        {
            "globals": {
                "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}"
            },
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-3333-49a3-80bd-e8fdd045185c}"
                },
                {
                    "name" : "profile2",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}"
                }
            ]
        })" };

        // Case 2: Bad settings
        Log::Comment(NoThrowString().Format(
            L"Testing a pair of profiles with the same guid"));
        const auto settingsObject = VerifyParseSucceeded(badProfiles);
        auto settings = CascadiaSettings::FromJson(settingsObject);

        settings->_ValidateSettings();

        VERIFY_ARE_EQUAL(static_cast<size_t>(2), settings->_warnings.size());
        VERIFY_ARE_EQUAL(::TerminalApp::SettingsLoadWarnings::DuplicateProfile, settings->_warnings.at(0));
        VERIFY_ARE_EQUAL(::TerminalApp::SettingsLoadWarnings::MissingDefaultProfile, settings->_warnings.at(1));

        VERIFY_ARE_EQUAL(static_cast<size_t>(2), settings->_profiles.size());
        VERIFY_ARE_EQUAL(settings->_globals.GetDefaultProfile(), settings->_profiles.at(0).GetGuid());
    }

}
