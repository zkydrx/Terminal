﻿// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AppKeyBindings.g.h"
#include "ActionArgs.h"
#include "..\inc\cppwinrt_utils.h"

namespace winrt::TerminalApp::implementation
{
    struct KeyChordHash
    {
        std::size_t operator()(const winrt::Microsoft::Terminal::Settings::KeyChord& key) const
        {
            std::hash<int32_t> keyHash;
            std::hash<winrt::Microsoft::Terminal::Settings::KeyModifiers> modifiersHash;
            std::size_t hashedKey = keyHash(key.Vkey());
            std::size_t hashedMods = modifiersHash(key.Modifiers());
            return hashedKey ^ hashedMods;
        }
    };

    struct KeyChordEquality
    {
        bool operator()(const winrt::Microsoft::Terminal::Settings::KeyChord& lhs, const winrt::Microsoft::Terminal::Settings::KeyChord& rhs) const
        {
            return lhs.Modifiers() == rhs.Modifiers() && lhs.Vkey() == rhs.Vkey();
        }
    };

    struct AppKeyBindings : AppKeyBindingsT<AppKeyBindings>
    {
        AppKeyBindings() = default;

        bool TryKeyChord(winrt::Microsoft::Terminal::Settings::KeyChord const& kc);
        void SetKeyBinding(TerminalApp::ShortcutAction const& action, winrt::Microsoft::Terminal::Settings::KeyChord const& chord);
        Microsoft::Terminal::Settings::KeyChord GetKeyBinding(TerminalApp::ShortcutAction const& action);

        static Windows::System::VirtualKeyModifiers ConvertVKModifiers(winrt::Microsoft::Terminal::Settings::KeyModifiers modifiers);

        // clang-format off
        TYPED_EVENT(CopyText,          TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(PasteText,         TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(NewTab,            TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(OpenNewTabDropdown,TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(DuplicateTab,      TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(NewTabWithProfile, TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(NewWindow,         TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(CloseWindow,       TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(CloseTab,          TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(ClosePane,         TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(SwitchToTab,       TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(NextTab,           TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(PrevTab,           TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(SplitVertical,     TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(SplitHorizontal,   TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(IncreaseFontSize,  TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(DecreaseFontSize,  TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(ScrollUp,          TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(ScrollDown,        TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(ScrollUpPage,      TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(ScrollDownPage,    TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(OpenSettings,      TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(ResizePane,        TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        TYPED_EVENT(MoveFocus,         TerminalApp::AppKeyBindings, TerminalApp::ActionEventArgs);
        // clang-format on

    private:
        std::unordered_map<winrt::Microsoft::Terminal::Settings::KeyChord, TerminalApp::ShortcutAction, KeyChordHash, KeyChordEquality> _keyShortcuts;
        bool _DoAction(ShortcutAction action);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    struct AppKeyBindings : AppKeyBindingsT<AppKeyBindings, implementation::AppKeyBindings>
    {
    };
}
