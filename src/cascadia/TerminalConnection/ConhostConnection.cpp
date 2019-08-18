// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ConhostConnection.h"
#include "windows.h"
#include <sstream>

#include "ConhostConnection.g.cpp"

#include <conpty-universal.h>
#include "../../types/inc/Utils.hpp"
#include "../../types/inc/UTF8OutPipeReader.hpp"

using namespace ::Microsoft::Console;

namespace winrt::Microsoft::Terminal::TerminalConnection::implementation
{
    ConhostConnection::ConhostConnection(const hstring& commandline,
                                         const hstring& startingDirectory,
                                         const hstring& startingTitle,
                                         const uint32_t initialRows,
                                         const uint32_t initialCols,
                                         const guid& initialGuid) :
        _initialRows{ initialRows },
        _initialCols{ initialCols },
        _commandline{ commandline },
        _startingDirectory{ startingDirectory },
        _startingTitle{ startingTitle },
        _guid{ initialGuid }
    {
        if (_guid == guid{})
        {
            _guid = Utils::CreateGuid();
        }
    }

    winrt::guid ConhostConnection::Guid() const noexcept
    {
        return _guid;
    }

    winrt::event_token ConhostConnection::TerminalOutput(Microsoft::Terminal::TerminalConnection::TerminalOutputEventArgs const& handler)
    {
        return _outputHandlers.add(handler);
    }

    void ConhostConnection::TerminalOutput(winrt::event_token const& token) noexcept
    {
        _outputHandlers.remove(token);
    }

    winrt::event_token ConhostConnection::TerminalDisconnected(Microsoft::Terminal::TerminalConnection::TerminalDisconnectedEventArgs const& handler)
    {
        return _disconnectHandlers.add(handler);
    }

    void ConhostConnection::TerminalDisconnected(winrt::event_token const& token) noexcept
    {
        _disconnectHandlers.remove(token);
    }

    void ConhostConnection::Start()
    {
        std::wstring cmdline{ _commandline.c_str() };
        std::optional<std::wstring> startingDirectory;
        if (!_startingDirectory.empty())
        {
            startingDirectory = _startingDirectory;
        }

        EnvironmentVariableMapW extraEnvVars;
        {
            // Convert connection Guid to string and ignore the enclosing '{}'.
            std::wstring wsGuid{ Utils::GuidToString(_guid) };
            wsGuid.pop_back();

            const wchar_t* const pwszGuid{ wsGuid.data() + 1 };

            // Ensure every connection has the unique identifier in the environment.
            extraEnvVars.emplace(L"WT_SESSION", pwszGuid);
        }

        // Create our own output handling thread
        // Each connection needs to make sure to drain the output from its backing host.
        _hOutputThread.reset(CreateThread(nullptr,
                                          0,
                                          StaticOutputThreadProc,
                                          this,
                                          0,
                                          nullptr));

        THROW_LAST_ERROR_IF_NULL(_hOutputThread);

        STARTUPINFO si = { 0 };
        si.cb = sizeof(STARTUPINFOW);

        // If we have a startingTitle, create a mutable character buffer to add
        // it to the STARTUPINFO.
        std::unique_ptr<wchar_t[]> mutableTitle{ nullptr };
        if (!_startingTitle.empty())
        {
            mutableTitle = std::make_unique<wchar_t[]>(_startingTitle.size() + 1);
            THROW_IF_NULL_ALLOC(mutableTitle);
            THROW_IF_FAILED(StringCchCopy(mutableTitle.get(), _startingTitle.size() + 1, _startingTitle.c_str()));
            si.lpTitle = mutableTitle.get();
        }

        THROW_IF_FAILED(
            CreateConPty(cmdline,
                         startingDirectory,
                         static_cast<short>(_initialCols),
                         static_cast<short>(_initialRows),
                         &_inPipe,
                         &_outPipe,
                         &_signalPipe,
                         &_piConhost,
                         0,
                         si,
                         extraEnvVars));

        _connected = true;
    }

    void ConhostConnection::WriteInput(hstring const& data)
    {
        if (!_connected || _closing.load())
        {
            return;
        }

        // convert from UTF-16LE to UTF-8 as ConPty expects UTF-8
        std::string str = winrt::to_string(data);
        bool fSuccess = !!WriteFile(_inPipe.get(), str.c_str(), (DWORD)str.length(), nullptr, nullptr);
        fSuccess;
    }

    void ConhostConnection::Resize(uint32_t rows, uint32_t columns)
    {
        if (!_connected)
        {
            _initialRows = rows;
            _initialCols = columns;
        }
        else if (!_closing.load())
        {
            SignalResizeWindow(_signalPipe.get(), Utils::ClampToShortMax(columns, 1), Utils::ClampToShortMax(rows, 1));
        }
    }

    void ConhostConnection::Close()
    {
        if (!_connected)
        {
            return;
        }

        if (!_closing.exchange(true))
        {
            // It is imperative that the signal pipe be closed first; this triggers the
            // pseudoconsole host's teardown. See PtySignalInputThread.cpp.
            _signalPipe.reset();
            _inPipe.reset();
            _outPipe.reset();

            // Tear down our output thread -- now that the output pipe was closed on the
            // far side, we can run down our local reader.
            WaitForSingleObject(_hOutputThread.get(), INFINITE);
            _hOutputThread.reset();

            // Wait for conhost to terminate.
            WaitForSingleObject(_piConhost.hProcess, INFINITE);

            _hJob.reset(); // This is a formality.
            _piConhost.reset();
        }
    }

    DWORD WINAPI ConhostConnection::StaticOutputThreadProc(LPVOID lpParameter)
    {
        ConhostConnection* const pInstance = (ConhostConnection*)lpParameter;
        return pInstance->_OutputThread();
    }

    DWORD ConhostConnection::_OutputThread()
    {
        UTF8OutPipeReader pipeReader{ _outPipe.get() };
        std::string_view strView{};

        // process the data of the output pipe in a loop
        while (true)
        {
            HRESULT result = pipeReader.Read(strView);
            if (FAILED(result) || result == S_FALSE)
            {
                if (_closing.load())
                {
                    // This is okay, break out to kill the thread
                    return 0;
                }

                _disconnectHandlers();
                return (DWORD)-1;
            }

            if (strView.empty())
            {
                return 0;
            }

            // Convert buffer to hstring
            auto hstr{ winrt::to_hstring(strView) };

            // Pass the output to our registered event handlers
            _outputHandlers(hstr);
        }

        return 0;
    }
}
