// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AzureConnection.h"
#include "AzureClientID.h"
#include "AzureConnectionStrings.h"
#include <sstream>
#include <stdlib.h>

#include "AzureConnection.g.cpp"

#include "../../types/inc/Utils.hpp"

using namespace ::Microsoft::Console;

using namespace utility;
using namespace web;
using namespace web::json;
using namespace web::http;
using namespace web::http::client;
using namespace web::websockets::client;
using namespace concurrency::streams;
using namespace winrt::Windows::Security::Credentials;

namespace winrt::Microsoft::Terminal::TerminalConnection::implementation
{
    // This file only builds for non-ARM64 so we don't need to check that here
    // This function exists because the clientID only gets added by the release pipelines
    // and is not available on local builds, so we want to be able to make sure we don't
    // try to make an Azure connection if its a local build
    bool AzureConnection::IsAzureConnectionAvailable()
    {
        return (AzureClientID != L"0");
    }

    AzureConnection::AzureConnection(const uint32_t initialRows, const uint32_t initialCols) :
        _initialRows{ initialRows },
        _initialCols{ initialCols }
    {
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - registers an output event handler
    // Arguments:
    // - the handler
    // Return value:
    // - the event token for the handler
    winrt::event_token AzureConnection::TerminalOutput(Microsoft::Terminal::TerminalConnection::TerminalOutputEventArgs const& handler)
    {
        return _outputHandlers.add(handler);
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - revokes an output event handler
    // Arguments:
    // - the event token for the handler
    void AzureConnection::TerminalOutput(winrt::event_token const& token) noexcept
    {
        _outputHandlers.remove(token);
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - registers a terminal-disconnected event handler
    // Arguments:
    // - the handler
    // Return value:
    // - the event token for the handler
    winrt::event_token AzureConnection::TerminalDisconnected(Microsoft::Terminal::TerminalConnection::TerminalDisconnectedEventArgs const& handler)
    {
        return _disconnectHandlers.add(handler);
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - revokes a terminal-disconnected event handler
    // Arguments:
    // - the event token for the handler
    void AzureConnection::TerminalDisconnected(winrt::event_token const& token) noexcept
    {
        _disconnectHandlers.remove(token);
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - creates the output thread (where we will do the authentication and actually connect to Azure)
    void AzureConnection::Start()
    {
        // Create our own output handling thread
        // Each connection needs to make sure to drain the output from its backing host.
        _hOutputThread.reset(CreateThread(nullptr,
                                          0,
                                          StaticOutputThreadProc,
                                          this,
                                          0,
                                          nullptr));

        THROW_LAST_ERROR_IF_NULL(_hOutputThread);

        _connected = true;
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - handles the different possible inputs in the different states
    // Arguments:
    // the user's input
    void AzureConnection::WriteInput(hstring const& data)
    {
        if (!_connected || _closing.load())
        {
            return;
        }

        // Parse the input differently depending on which state we're in
        switch (_state)
        {
        // The user has stored connection settings, let them choose one of them, create a new one or remove all stored ones
        case State::AccessStored:
        {
            const auto s = winrt::to_string(data);
            int storeNum = -1;
            try
            {
                storeNum = std::stoi(s);
            }
            catch (...)
            {
                if (s != "n" && s != "r")
                {
                    _outputHandlers(winrt::to_hstring(invalidAccessInput));
                    return;
                }
                else if (s == "r")
                {
                    std::lock_guard<std::mutex> lg{ _commonMutex };
                    _removeOrNew = true;
                    _canProceed.notify_one();
                    return;
                }
                else
                {
                    std::lock_guard<std::mutex> lg{ _commonMutex };
                    _removeOrNew = false;
                    _canProceed.notify_one();
                    return;
                }
            }
            if (storeNum >= _maxStored)
            {
                _outputHandlers(winrt::to_hstring(numOutOfBoundsError));
                return;
            }
            std::lock_guard<std::mutex> lg{ _commonMutex };
            _storedNumber = storeNum;
            _canProceed.notify_one();
            return;
        }
        // The user has multiple tenants in their Azure account, let them choose one of them
        case State::TenantChoice:
        {
            int tenantNum = -1;
            try
            {
                tenantNum = std::stoi(winrt::to_string(data));
            }
            catch (...)
            {
                _outputHandlers(winrt::to_hstring(nonNumberError));
                return;
            }
            if (tenantNum >= _maxSize)
            {
                _outputHandlers(winrt::to_hstring(numOutOfBoundsError));
                return;
            }
            std::lock_guard<std::mutex> lg{ _commonMutex };
            _tenantNumber = tenantNum;
            _canProceed.notify_one();
            return;
        }
        // User has the option to save their connection settings for future logins
        case State::StoreTokens:
        {
            std::string s = winrt::to_string(data);
            if (s != "y" && s != "n")
            {
                _outputHandlers(winrt::to_hstring(invalidStoreInput));
            }
            else if (s == "y")
            {
                std::lock_guard<std::mutex> lg{ _commonMutex };
                _store = true;
                _canProceed.notify_one();
            }
            else
            {
                std::lock_guard<std::mutex> lg{ _commonMutex };
                _store = false;
                _canProceed.notify_one();
            }
            return;
        }
        // We are connected, send user's input over the websocket
        case State::TermConnected:
        {
            websocket_outgoing_message msg;
            const auto str = winrt::to_string(data);
            msg.set_utf8_message(str);

            _cloudShellSocket.send(msg);
        }
        default:
            return;
        }
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - resizes the terminal
    // Arguments:
    // - the new rows/cols values
    void AzureConnection::Resize(uint32_t rows, uint32_t columns)
    {
        if (!_connected || !(_state == State::TermConnected))
        {
            _initialRows = rows;
            _initialCols = columns;
        }
        else if (!_closing.load())
        {
            // Initialize client
            http_client terminalClient(_cloudShellUri);

            // Initialize the request
            http_request terminalRequest(L"POST");
            terminalRequest.set_request_uri(L"terminals/" + _terminalID + L"/size?cols=" + std::to_wstring(columns) + L"&rows=" + std::to_wstring(rows) + L"&version=2019-01-01");
            _HeaderHelper(terminalRequest);
            terminalRequest.set_body(json::value(L""));

            // Send the request
            _RequestHelper(terminalClient, terminalRequest);
        }
    }

    // Method description:
    // - ascribes to the ITerminalConnection interface
    // - closes the websocket connection and the output thread
    void AzureConnection::Close()
    {
        if (!_connected)
        {
            return;
        }

        if (!_closing.exchange(true))
        {
            _canProceed.notify_all();
            if (_state == State::TermConnected)
            {
                // Close the websocket connection
                auto closedTask = _cloudShellSocket.close();
                closedTask.wait();
            }

            // Tear down our output thread
            WaitForSingleObject(_hOutputThread.get(), INFINITE);
            _hOutputThread.reset();
        }
    }

    // Method description:
    // - this method bridges the thread to the Azure connection instance
    // Arguments:
    // - lpParameter: the Azure connection parameter
    // Return value:
    // - the exit code of the thread
    DWORD WINAPI AzureConnection::StaticOutputThreadProc(LPVOID lpParameter)
    {
        AzureConnection* const pInstance = (AzureConnection*)lpParameter;
        return pInstance->_OutputThread();
    }

    // Method description:
    // - this is the output thread, where we initiate the connection to Azure
    //  and establish a websocket connection
    // Return value:
    // - return status
    DWORD AzureConnection::_OutputThread()
    {
        while (true)
        {
            try
            {
                switch (_state)
                {
                // Initial state, check if the user has any stored connection settings and allow them to login with those
                // or allow them to login with a different account or allow them to remove the saved settings
                case State::AccessStored:
                {
                    RETURN_IF_FAILED(_AccessHelper());
                    break;
                }
                // User has no saved connection settings or has opted to login with a different account
                // Azure authentication happens here
                case State::DeviceFlow:
                {
                    RETURN_IF_FAILED(_DeviceFlowHelper());
                    break;
                }
                // User has multiple tenants in their Azure account, they need to choose which one to connect to
                case State::TenantChoice:
                {
                    RETURN_IF_FAILED(_TenantChoiceHelper());
                    break;
                }
                // Ask the user if they want to save these connection settings for future logins
                case State::StoreTokens:
                {
                    RETURN_IF_FAILED(_StoreHelper());
                    break;
                }
                // Connect to Azure, we only get here once we have everything we need (tenantID, accessToken, refreshToken)
                case State::TermConnecting:
                {
                    RETURN_IF_FAILED(_ConnectHelper());
                    break;
                }
                // We are connected, continuously read from the websocket until its closed
                case State::TermConnected:
                {
                    while (true)
                    {
                        // Read from websocket
                        pplx::task<websocket_incoming_message> msgT;
                        try
                        {
                            msgT = _cloudShellSocket.receive();
                            msgT.wait();
                        }
                        catch (...)
                        {
                            // Websocket has been closed
                            if (!_closing.load())
                            {
                                _state = State::NoConnect;
                                _disconnectHandlers();
                                return S_FALSE;
                            }
                            break;
                        }
                        auto msg = msgT.get();
                        auto msgStringTask = msg.extract_string();
                        auto msgString = msgStringTask.get();

                        // Convert to hstring
                        const auto hstr = winrt::to_hstring(msgString);

                        // Pass the output to our registered event handlers
                        _outputHandlers(hstr);
                    }
                    return S_OK;
                }
                case State::NoConnect:
                {
                    _outputHandlers(winrt::to_hstring(internetOrServerIssue));
                    _disconnectHandlers();
                    return E_FAIL;
                }
                }
            }
            catch (...)
            {
                _state = State::NoConnect;
            }
        }
    }

    // Method description:
    // - helper function to get the stored credentials (if any) and let the user choose what to do next
    // Return value:
    // - S_FALSE if there are no stored credentials
    // - S_OK if the user opts to login with a stored set of credentials or login with a different account
    // - E_FAIL if the user closes the tab
    HRESULT AzureConnection::_AccessHelper()
    {
        auto vault = PasswordVault();
        winrt::Windows::Foundation::Collections::IVectorView<PasswordCredential> credList;
        // FindAllByResource throws an exception if there are no credentials stored under the given resource so we wrap it in a try-catch block
        try
        {
            credList = vault.FindAllByResource(resource);
        }
        catch (...)
        {
            // No credentials are stored, so start the device flow
            _state = State::DeviceFlow;
            return S_FALSE;
        }
        _maxStored = credList.Size();
        // Display the user's saved connection settings
        for (int i = 0; i < _maxStored; i++)
        {
            auto entry = credList.GetAt(i);
            auto nameJson = json::value::parse(entry.UserName().c_str());
            _outputHandlers(_StrFormatHelper(ithTenant, i, nameJson.at(L"displayName").as_string().c_str(), nameJson.at(L"tenantID").as_string().c_str()));
        }

        _outputHandlers(winrt::to_hstring(enterTenant));
        _outputHandlers(winrt::to_hstring(newLogin));
        _outputHandlers(winrt::to_hstring(removeStored));

        std::unique_lock<std::mutex> storedLock{ _commonMutex };
        _canProceed.wait(storedLock, [=]() {
            return (_storedNumber >= 0 && _storedNumber < _maxStored) || _removeOrNew.has_value() || _closing.load();
        });
        // User might have closed the tab while we waited for input
        if (_closing.load())
        {
            return E_FAIL;
        }
        else if (_removeOrNew.has_value() && _removeOrNew.value())
        {
            // User wants to remove the stored settings
            _RemoveCredentials();
            _state = State::DeviceFlow;
            return S_OK;
        }
        else if (_removeOrNew.has_value() && !_removeOrNew.value())
        {
            // User wants to login with a different account
            _state = State::DeviceFlow;
            return S_OK;
        }
        // User wants to login with one of the saved connection settings
        auto desiredCredential = credList.GetAt(_storedNumber);
        desiredCredential.RetrievePassword();
        auto nameJson = json::value::parse(desiredCredential.UserName().c_str());
        auto passWordJson = json::value::parse(desiredCredential.Password().c_str());
        _displayName = nameJson.at(L"displayName").as_string();
        _tenantID = nameJson.at(L"tenantID").as_string();
        _accessToken = passWordJson.at(L"accessToken").as_string();
        _refreshToken = passWordJson.at(L"refreshToken").as_string();
        _expiry = std::stoi(passWordJson.at(L"expiry").as_string());

        const auto t1 = std::chrono::system_clock::now();
        const auto timeNow = std::chrono::duration_cast<std::chrono::seconds>(t1.time_since_epoch()).count();

        // Check if the token is close to expiring and refresh if so
        if (timeNow + _expireLimit > _expiry)
        {
            const auto refreshResponse = _RefreshTokens();
            _accessToken = refreshResponse.at(L"access_token").as_string();
            _refreshToken = refreshResponse.at(L"refresh_token").as_string();
            _expiry = std::stoi(refreshResponse.at(L"expires_on").as_string());
            // Store the updated tokens under the same username
            _StoreCredential();
        }

        // We have everything we need, so go ahead and connect
        _state = State::TermConnecting;
        return S_OK;
    }

    // Method description:
    // - helper function to start the device code flow (required for authentication to Azure)
    // Return value:
    // - E_FAIL if the user closes the tab, does not authenticate in time or has no tenants in their Azure account
    // - S_OK otherwise
    HRESULT AzureConnection::_DeviceFlowHelper()
    {
        // Initiate device code flow
        const auto deviceCodeResponse = _GetDeviceCode();

        // Print the message and store the device code, polling interval and expiry
        const auto message = deviceCodeResponse.at(L"message").as_string();
        _outputHandlers(message + codeExpiry);
        const auto devCode = deviceCodeResponse.at(L"device_code").as_string();
        const auto pollInterval = std::stoi(deviceCodeResponse.at(L"interval").as_string());
        const auto expiresIn = std::stoi(deviceCodeResponse.at(L"expires_in").as_string());

        // Wait for user authentication and obtain the access/refresh tokens
        json::value authenticatedResponse;
        try
        {
            authenticatedResponse = _WaitForUser(devCode, pollInterval, expiresIn);
        }
        catch (...)
        {
            _outputHandlers(winrt::to_hstring(exitStr));
            return E_FAIL;
        }

        _accessToken = authenticatedResponse.at(L"access_token").as_string();
        _refreshToken = authenticatedResponse.at(L"refresh_token").as_string();

        // Get the tenants and the required tenant id
        const auto tenantsResponse = _GetTenants();
        _tenantList = tenantsResponse.at(L"value");
        const auto tenantListAsArray = _tenantList.as_array();
        if (tenantListAsArray.size() == 0)
        {
            _outputHandlers(winrt::to_hstring(noTenants));
            return E_FAIL;
        }
        else if (_tenantList.size() == 1)
        {
            _tenantID = tenantListAsArray.at(0).at(L"tenantId").as_string();
            _displayName = tenantListAsArray.at(0).at(L"displayName").as_string();

            // We have to refresh now that we have the tenantID
            const auto refreshResponse = _RefreshTokens();
            _accessToken = refreshResponse.at(L"access_token").as_string();
            _refreshToken = refreshResponse.at(L"refresh_token").as_string();
            _expiry = std::stoi(refreshResponse.at(L"expires_on").as_string());

            _state = State::StoreTokens;
        }
        else
        {
            _state = State::TenantChoice;
        }
        return S_OK;
    }

    // Method description:
    // - helper function to list the user's tenants and let them decide which tenant they wish to connect to
    // Return value:
    // - E_FAIL if the user closes the tab
    // - S_OK otherwise
    HRESULT AzureConnection::_TenantChoiceHelper()
    {
        const auto tenantListAsArray = _tenantList.as_array();
        _maxSize = tenantListAsArray.size();
        for (int i = 0; i < _maxSize; i++)
        {
            _outputHandlers(_StrFormatHelper(ithTenant, i, tenantListAsArray.at(i).at(L"displayName").as_string().c_str(), tenantListAsArray.at(i).at(L"tenantId").as_string().c_str()));
        }
        _outputHandlers(winrt::to_hstring(enterTenant));
        // Use a lock to wait for the user to input a valid number
        std::unique_lock<std::mutex> tenantNumberLock{ _commonMutex };
        _canProceed.wait(tenantNumberLock, [=]() {
            return (_tenantNumber >= 0 && _tenantNumber < _maxSize) || _closing.load();
        });
        // User might have closed the tab while we waited for input
        if (_closing.load())
        {
            return E_FAIL;
        }
        _tenantID = tenantListAsArray.at(_tenantNumber).at(L"tenantId").as_string();
        _displayName = tenantListAsArray.at(_tenantNumber).at(L"displayName").as_string();

        // We have to refresh now that we have the tenantID
        const auto refreshResponse = _RefreshTokens();
        _accessToken = refreshResponse.at(L"access_token").as_string();
        _refreshToken = refreshResponse.at(L"refresh_token").as_string();
        _expiry = std::stoi(refreshResponse.at(L"expires_on").as_string());

        _state = State::StoreTokens;
        return S_OK;
    }

    // Method description:
    // - helper function to ask the user if they wish to store their credentials
    // Return value:
    // - E_FAIL if the user closes the tab
    // - S_OK otherwise
    HRESULT AzureConnection::_StoreHelper()
    {
        _outputHandlers(winrt::to_hstring(storePrompt));
        // Wait for user input
        std::unique_lock<std::mutex> storeLock{ _commonMutex };
        _canProceed.wait(storeLock, [=]() {
            return _store.has_value() || _closing.load();
        });
        // User might have closed the tab while we waited for input
        if (_closing.load())
        {
            return E_FAIL;
        }

        if (_store.value())
        {
            // User has opted to store the connection settings
            _StoreCredential();
            _outputHandlers(winrt::to_hstring(tokensStored));
        }

        _state = State::TermConnecting;
        return S_OK;
    }

    // Method description:
    // - helper function to connect the user to the Azure cloud shell
    // Return value:
    // - E_FAIL if the user has not set up their cloud shell yet
    // - S_OK after successful connection
    HRESULT AzureConnection::_ConnectHelper()
    {
        // Get user's cloud shell settings
        const auto settingsResponse = _GetCloudShellUserSettings();
        if (settingsResponse.has_field(L"error"))
        {
            _outputHandlers(winrt::to_hstring(noCloudAccount));
            return E_FAIL;
        }

        // Request for a cloud shell
        _outputHandlers(winrt::to_hstring(requestingCloud));
        _cloudShellUri = _GetCloudShell();
        _outputHandlers(winrt::to_hstring(success));

        // Request for a terminal for said cloud shell
        // We only support bash for now, so don't bother with the user's preferred shell
        // fyi: we can't call powershell yet because it sends VT sequences we don't support yet
        // TODO: GitHub #1883
        //const auto shellType = settingsResponse.at(L"properties").at(L"preferredShellType").as_string();
        const auto shellType = L"bash";
        _outputHandlers(winrt::to_hstring(requestingTerminal));
        const auto socketUri = _GetTerminal(shellType);
        _outputHandlers(winrt::to_hstring("\r\n"));

        // Step 8: connecting to said terminal
        const auto connReqTask = _cloudShellSocket.connect(socketUri);
        connReqTask.wait();

        _state = State::TermConnected;
        return S_OK;
    }

    // Method description:
    // - helper function to send requests and extract responses as json values
    // Arguments:
    // - a http_client
    // - a http_request for the client to send
    // Return value:
    // - the response from the server as a json value
    json::value AzureConnection::_RequestHelper(http_client theClient, http_request theRequest)
    {
        json::value jsonResult;
        try
        {
            const auto responseTask = theClient.request(theRequest);
            responseTask.wait();
            const auto response = responseTask.get();
            const auto responseJsonTask = response.extract_json();
            responseJsonTask.wait();
            jsonResult = responseJsonTask.get();
        }
        catch (...)
        {
            _outputHandlers(winrt::to_hstring(internetOrServerIssue));
        }
        return jsonResult;
    }

    // Method description:
    // - helper function to start the device code flow
    // Return value:
    // - the response to the device code flow initiation
    json::value AzureConnection::_GetDeviceCode()
    {
        // Initialize the client
        http_client loginClient(_loginUri);

        // Initialize the request
        http_request commonRequest(L"POST");
        commonRequest.set_request_uri(L"common/oauth2/devicecode");
        const auto body = L"client_id=" + AzureClientID + L"&resource=" + _wantedResource;
        commonRequest.set_body(body, L"application/x-www-form-urlencoded");

        // Send the request and receive the response as a json value
        return _RequestHelper(loginClient, commonRequest);
    }

    // Method description:
    // - helper function to wait for the user to authenticate using their web browser
    // Arguments:
    // - the device code that would have been received when authentication was initiated
    // - the polling interval duration
    // - the duration the code is still valid for
    // Return value:
    // - if authentication is done successfully, then return the response from the server
    // - else, throw an exception
    json::value AzureConnection::_WaitForUser(const utility::string_t deviceCode, int pollInterval, int expiresIn)
    {
        // Initialize the client
        http_client pollingClient(_loginUri);

        // Continuously send a poll request until the user authenticates
        const auto body = L"grant_type=device_code&resource=" + _wantedResource + L"&client_id=" + AzureClientID + L"&code=" + deviceCode;
        const auto requestUri = L"common/oauth2/token";
        json::value responseJson;
        for (int count = 0; count < expiresIn / pollInterval; count++)
        {
            // User might close the tab while we wait for them to authenticate, this case handles that
            if (_closing.load())
            {
                throw "Tab closed.";
            }
            http_request pollRequest(L"POST");
            pollRequest.set_request_uri(requestUri);
            pollRequest.set_body(body, L"application/x-www-form-urlencoded");

            responseJson = _RequestHelper(pollingClient, pollRequest);

            if (responseJson.has_field(L"error"))
            {
                Sleep(pollInterval * 1000); // Sleep takes arguments in milliseconds
                continue; // Still waiting for authentication
            }
            else
            {
                _outputHandlers(winrt::to_hstring("Authenticated.\r\n"));
                break; // Authentication is done, break from loop
            }
        }
        if (responseJson.has_field(L"error"))
        {
            throw "Time out.";
        }
        return responseJson;
    }

    // Method description:
    // - helper function to acquire the user's Azure tenants
    // Return value:
    // - the response which contains a list of the user's Azure tenants
    json::value AzureConnection::_GetTenants()
    {
        // Initialize the client
        http_client tenantClient(_resourceUri);

        // Initialize the request
        http_request tenantRequest(L"GET");
        tenantRequest.set_request_uri(L"tenants?api-version=2018-01-01");
        _HeaderHelper(tenantRequest);

        // Send the request and return the response as a json value
        return _RequestHelper(tenantClient, tenantRequest);
    }

    // Method description:
    // - helper function to refresh the access/refresh tokens
    // Return value:
    // - the response with the new tokens
    json::value AzureConnection::_RefreshTokens()
    {
        // Initialize the client
        http_client refreshClient(_loginUri);

        // Initialize the request
        http_request refreshRequest(L"POST");
        refreshRequest.set_request_uri(_tenantID + L"/oauth2/token");
        const auto body = L"client_id=" + AzureClientID + L"&resource=" + _wantedResource + L"&grant_type=refresh_token" + L"&refresh_token=" + _refreshToken;
        refreshRequest.set_body(body, L"application/x-www-form-urlencoded");
        refreshRequest.headers().add(L"User-Agent", userAgent);

        // Send the request and return the response as a json value
        return _RequestHelper(refreshClient, refreshRequest);
    }

    // Method description:
    // - helper function to get the user's cloud shell settings
    // Return value:
    // - the user's cloud shell settings
    json::value AzureConnection::_GetCloudShellUserSettings()
    {
        // Initialize client
        http_client settingsClient(_resourceUri);

        // Initialize request
        http_request settingsRequest(L"GET");
        settingsRequest.set_request_uri(L"providers/Microsoft.Portal/userSettings/cloudconsole?api-version=2018-10-01");
        _HeaderHelper(settingsRequest);

        return _RequestHelper(settingsClient, settingsRequest);
    }

    // Method description:
    // - helper function to request for a cloud shell
    // Return value:
    // - the uri for the cloud shell
    utility::string_t AzureConnection::_GetCloudShell()
    {
        // Initialize client
        http_client cloudShellClient(_resourceUri);

        // Initialize request
        http_request shellRequest(L"PUT");
        shellRequest.set_request_uri(L"providers/Microsoft.Portal/consoles/default?api-version=2018-10-01");
        _HeaderHelper(shellRequest);
        const auto innerBody = json::value::parse(U("{ \"osType\" : \"linux\" }"));
        json::value body;
        body[U("properties")] = innerBody;
        shellRequest.set_body(body);

        // Send the request and get the response as a json value
        const auto cloudShell = _RequestHelper(cloudShellClient, shellRequest);

        // Return the uri
        return cloudShell.at(L"properties").at(L"uri").as_string() + L"/";
    }

    // Method description:
    // - helper function to request for a terminal
    // Return value:
    // - the uri for the terminal
    utility::string_t AzureConnection::_GetTerminal(utility::string_t shellType)
    {
        // Initialize client
        http_client terminalClient(_cloudShellUri);

        // Initialize the request
        http_request terminalRequest(L"POST");
        terminalRequest.set_request_uri(L"terminals?cols=" + std::to_wstring(_initialCols) + L"&rows=" + std::to_wstring(_initialRows) + L"&version=2019-01-01&shell=" + shellType);
        _HeaderHelper(terminalRequest);

        // Send the request and get the response as a json value
        const auto terminalResponse = _RequestHelper(terminalClient, terminalRequest);
        _terminalID = terminalResponse.at(L"id").as_string();

        // Return the uri
        return terminalResponse.at(L"socketUri").as_string();
    }

    // Method description:
    // - helper function to set the headers of a http_request
    // Arguments:
    // - the http_request
    void AzureConnection::_HeaderHelper(http_request theRequest)
    {
        theRequest.headers().add(L"Accept", L"application/json");
        theRequest.headers().add(L"Content-Type", L"application/json");
        theRequest.headers().add(L"Authorization", L"Bearer " + _accessToken);
        theRequest.headers().add(L"User-Agent", userAgent);
    }

    // Method description:
    // - helper function to store the credentials
    // - we store the display name, tenant ID, access/refresh tokens, and token expiry
    void AzureConnection::_StoreCredential()
    {
        auto vault = PasswordVault();
        json::value userName;
        userName[U("displayName")] = json::value::string(_displayName);
        userName[U("tenantID")] = json::value::string(_tenantID);
        json::value passWord;
        passWord[U("accessToken")] = json::value::string(_accessToken);
        passWord[U("refreshToken")] = json::value::string(_refreshToken);
        passWord[U("expiry")] = json::value::string(std::to_wstring(_expiry));
        auto newCredential = PasswordCredential(resource, userName.serialize(), passWord.serialize());
        vault.Add(newCredential);
    }

    // Method description:
    // - helper function to remove all stored credentials
    void AzureConnection::_RemoveCredentials()
    {
        auto vault = PasswordVault();
        winrt::Windows::Foundation::Collections::IVectorView<PasswordCredential> credList;
        // FindAllByResource throws an exception if there are no credentials stored under the given resource so we wrap it in a try-catch block
        try
        {
            credList = vault.FindAllByResource(resource);
        }
        catch (...)
        {
            // No credentials are stored, so just return
            _outputHandlers(winrt::to_hstring(noTokens));
            return;
        }
        while (credList.Size() > 0)
        {
            try
            {
                vault.Remove(credList.GetAt(0));
            }
            catch (...)
            {
                _outputHandlers(winrt::to_hstring(tokensRemoved));
                return;
            }
        }
    }

    std::wstring AzureConnection::_StrFormatHelper(const wchar_t* const format, int i, const wchar_t* name, const wchar_t* ID)
    {
        const auto lengthRequired = _scwprintf(ithTenant, i, name, ID);
        std::wstring buffer;
        buffer.resize(lengthRequired + 1);
        swprintf_s(buffer.data(), buffer.size(), ithTenant, i, name, ID);
        return buffer;
    }
}
