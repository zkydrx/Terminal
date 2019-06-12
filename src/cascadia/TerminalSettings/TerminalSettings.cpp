// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TerminalSettings.h"
#include <DefaultSettings.h>

#include "TerminalSettings.g.cpp"

namespace winrt::Microsoft::Terminal::Settings::implementation
{
    TerminalSettings::TerminalSettings() :
        _defaultForeground{ DEFAULT_FOREGROUND_WITH_ALPHA },
        _defaultBackground{ DEFAULT_BACKGROUND_WITH_ALPHA },
        _colorTable{},
        _historySize{ DEFAULT_HISTORY_SIZE },
        _initialRows{ 30 },
        _initialCols{ 80 },
        _snapOnInput{ true },
        _cursorColor{ DEFAULT_CURSOR_COLOR },
        _cursorShape{ CursorStyle::Vintage },
        _cursorHeight{ DEFAULT_CURSOR_HEIGHT },
        _useAcrylic{ false },
        _closeOnExit{ true },
        _tintOpacity{ 0.5 },
        _padding{ DEFAULT_PADDING },
        _fontFace{ DEFAULT_FONT_FACE },
        _fontSize{ DEFAULT_FONT_SIZE },
        _backgroundImage{},
        _backgroundImageOpacity{ 1.0 },
        _backgroundImageStretchMode{ winrt::Windows::UI::Xaml::Media::Stretch::UniformToFill },
        _keyBindings{ nullptr },
        _scrollbarState{ ScrollbarState::Visible }
    {
    }

    uint32_t TerminalSettings::DefaultForeground()
    {
        return _defaultForeground;
    }

    void TerminalSettings::DefaultForeground(uint32_t value)
    {
        _defaultForeground = value;
    }

    uint32_t TerminalSettings::DefaultBackground()
    {
        return _defaultBackground;
    }

    void TerminalSettings::DefaultBackground(uint32_t value)
    {
        _defaultBackground = value;
    }

    uint32_t TerminalSettings::GetColorTableEntry(int32_t index) const
    {
        return _colorTable[index];
    }

    void TerminalSettings::SetColorTableEntry(int32_t index, uint32_t value)
    {
        THROW_HR_IF(E_INVALIDARG, index > _colorTable.size());
        _colorTable[index] = value;
    }

    int32_t TerminalSettings::HistorySize()
    {
        return _historySize;
    }

    void TerminalSettings::HistorySize(int32_t value)
    {
        _historySize = value;
    }

    int32_t TerminalSettings::InitialRows()
    {
        return _initialRows;
    }

    void TerminalSettings::InitialRows(int32_t value)
    {
        _initialRows = value;
    }

    int32_t TerminalSettings::InitialCols()
    {
        return _initialCols;
    }

    void TerminalSettings::InitialCols(int32_t value)
    {
        _initialCols = value;
    }

    bool TerminalSettings::SnapOnInput()
    {
        return _snapOnInput;
    }

    void TerminalSettings::SnapOnInput(bool value)
    {
        _snapOnInput = value;
    }

    uint32_t TerminalSettings::CursorColor()
    {
        return _cursorColor;
    }

    void TerminalSettings::CursorColor(uint32_t value)
    {
        _cursorColor = value;
    }

    Settings::CursorStyle TerminalSettings::CursorShape() const noexcept
    {
        return _cursorShape;
    }

    void TerminalSettings::CursorShape(Settings::CursorStyle const& value) noexcept
    {
        _cursorShape = value;
    }

    uint32_t TerminalSettings::CursorHeight()
    {
        return _cursorHeight;
    }

    void TerminalSettings::CursorHeight(uint32_t value)
    {
        _cursorHeight = value;
    }

    bool TerminalSettings::UseAcrylic()
    {
        return _useAcrylic;
    }

    void TerminalSettings::UseAcrylic(bool value)
    {
        _useAcrylic = value;
    }

    bool TerminalSettings::CloseOnExit()
    {
        return _closeOnExit;
    }

    void TerminalSettings::CloseOnExit(bool value)
    {
        _closeOnExit = value;
    }

    double TerminalSettings::TintOpacity()
    {
        return _tintOpacity;
    }

    void TerminalSettings::TintOpacity(double value)
    {
        _tintOpacity = value;
    }

    hstring TerminalSettings::Padding()
    {
        return _padding;
    }

    void TerminalSettings::Padding(hstring value)
    {
        _padding = value;
    }

    hstring TerminalSettings::FontFace()
    {
        return _fontFace;
    }

    void TerminalSettings::FontFace(hstring const& value)
    {
        _fontFace = value;
    }

    int32_t TerminalSettings::FontSize()
    {
        return _fontSize;
    }

    void TerminalSettings::FontSize(int32_t value)
    {
        _fontSize = value;
    }

    void TerminalSettings::BackgroundImage(hstring const& value)
    {
        _backgroundImage = value;
    }

    hstring TerminalSettings::BackgroundImage()
    {
        return _backgroundImage;
    }

    void TerminalSettings::BackgroundImageOpacity(double value)
    {
        _backgroundImageOpacity = value;
    }

    double TerminalSettings::BackgroundImageOpacity()
    {
        return _backgroundImageOpacity;
    }

    winrt::Windows::UI::Xaml::Media::Stretch TerminalSettings::BackgroundImageStretchMode()
    {
        return _backgroundImageStretchMode;
    }

    void TerminalSettings::BackgroundImageStretchMode(winrt::Windows::UI::Xaml::Media::Stretch value)
    {
        _backgroundImageStretchMode = value;
    }

    Settings::IKeyBindings TerminalSettings::KeyBindings()
    {
        return _keyBindings;
    }

    void TerminalSettings::KeyBindings(Settings::IKeyBindings const& value)
    {
        _keyBindings = value;
    }

    hstring TerminalSettings::Commandline()
    {
        return _commandline;
    }

    void TerminalSettings::Commandline(hstring const& value)
    {
        _commandline = value;
    }

    hstring TerminalSettings::StartingDirectory()
    {
        return _startingDir;
    }

    void TerminalSettings::StartingDirectory(hstring const& value)
    {
        _startingDir = value;
    }

    hstring TerminalSettings::EnvironmentVariables()
    {
        return _envVars;
    }

    void TerminalSettings::EnvironmentVariables(hstring const& value)
    {
        _envVars = value;
    }

    Settings::ScrollbarState TerminalSettings::ScrollState() const noexcept
    {
        return _scrollbarState;
    }

    void TerminalSettings::ScrollState(Settings::ScrollbarState const& value) noexcept
    {
        _scrollbarState = value;
    }

}
