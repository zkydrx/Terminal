// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AzureConnection.g.h"

#include <cpprest/http_client.h>
#include <cpprest/http_listener.h>
#include <cpprest/ws_client.h>
#include <mutex>
#include <condition_variable>

#include "../cascadia/inc/cppwinrt_utils.h"
#include "ConnectionStateHolder.h"

namespace winrt::Microsoft::Terminal::TerminalConnection::implementation
{
    struct AzureConnection : AzureConnectionT<AzureConnection>, ConnectionStateHolder<AzureConnection>
    {
        static bool IsAzureConnectionAvailable() noexcept;
        AzureConnection(const uint32_t rows, const uint32_t cols);

        void Start();
        void WriteInput(hstring const& data);
        void Resize(uint32_t rows, uint32_t columns);
        void Close();

        WINRT_CALLBACK(TerminalOutput, TerminalOutputHandler);

    private:
        uint32_t _initialRows{};
        uint32_t _initialCols{};

        enum class AzureState
        {
            AccessStored,
            DeviceFlow,
            TenantChoice,
            StoreTokens,
            TermConnecting,
            TermConnected,
            NoConnect
        };

        AzureState _state{ AzureState::AccessStored };

        wil::unique_handle _hOutputThread;

        static DWORD WINAPI StaticOutputThreadProc(LPVOID lpParameter);
        DWORD _OutputThread();
        HRESULT _AccessHelper();
        HRESULT _DeviceFlowHelper();
        HRESULT _TenantChoiceHelper();
        HRESULT _StoreHelper();
        HRESULT _ConnectHelper();

        const utility::string_t _loginUri{ U("https://login.microsoftonline.com/") };
        const utility::string_t _resourceUri{ U("https://management.azure.com/") };
        const utility::string_t _wantedResource{ U("https://management.core.windows.net/") };
        const int _expireLimit{ 2700 };
        web::json::value _tenantList;
        utility::string_t _displayName;
        utility::string_t _tenantID;
        utility::string_t _accessToken;
        utility::string_t _refreshToken;
        int _expiry{ 0 };
        utility::string_t _cloudShellUri;
        utility::string_t _terminalID;

        void _WriteStringWithNewline(const std::wstring_view str);
        web::json::value _RequestHelper(web::http::client::http_client theClient, web::http::http_request theRequest);
        web::json::value _GetDeviceCode();
        web::json::value _WaitForUser(utility::string_t deviceCode, int pollInterval, int expiresIn);
        web::json::value _GetTenants();
        web::json::value _RefreshTokens();
        web::json::value _GetCloudShellUserSettings();
        utility::string_t _GetCloudShell();
        utility::string_t _GetTerminal(utility::string_t shellType);
        void _HeaderHelper(web::http::http_request theRequest);
        void _StoreCredential();
        void _RemoveCredentials();

        enum class InputMode
        {
            None = 0,
            Line
        };
        InputMode _currentInputMode{ InputMode::None };
        std::wstring _userInput;
        std::condition_variable _inputEvent;
        std::mutex _inputMutex;

        std::optional<std::wstring> _ReadUserInput(InputMode mode);

        web::websockets::client::websocket_client _cloudShellSocket;
    };
}

namespace winrt::Microsoft::Terminal::TerminalConnection::factory_implementation
{
    struct AzureConnection : AzureConnectionT<AzureConnection, implementation::AzureConnection>
    {
    };
}
