// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

/*++
Module Name:
- cppwinrt_utils.h

Abstract:
- This module is used for winrt event declarations/definitions

Author(s):
- Carlos Zamora (CaZamor) 23-Apr-2019

Revision History:
- N/A
--*/

#pragma once

// This is a helper macro to make declaring events easier.
// This will declare the event handler and the methods for adding and removing a
// handler callback from the event
#define DECLARE_EVENT(name, eventHandler, args)          \
public:                                                  \
    winrt::event_token name(args const& handler);        \
    void name(winrt::event_token const& token) noexcept; \
                                                         \
private:                                                 \
    winrt::event<args> eventHandler;

// This is a helper macro for defining the body of events.
// Winrt events need a method for adding a callback to the event and removing
//      the callback. This macro will define them both for you, because they
//      don't really vary from event to event.
#define DEFINE_EVENT(className, name, eventHandler, args)                                         \
    winrt::event_token className::name(args const& handler) { return eventHandler.add(handler); } \
    void className::name(winrt::event_token const& token) noexcept { eventHandler.remove(token); }

// This is a helper macro to make declaring events easier.
// This will declare the event handler and the methods for adding and removing a
// handler callback from the event.
// Use this if you have a Windows.Foundation.TypedEventHandler
#define DECLARE_EVENT_WITH_TYPED_EVENT_HANDLER(name, eventHandler, sender, args)                  \
public:                                                                                           \
    winrt::event_token name(Windows::Foundation::TypedEventHandler<sender, args> const& handler); \
    void name(winrt::event_token const& token) noexcept;                                          \
                                                                                                  \
private:                                                                                          \
    winrt::event<Windows::Foundation::TypedEventHandler<sender, args>> eventHandler;

// This is a helper macro for defining the body of events.
// Winrt events need a method for adding a callback to the event and removing
//      the callback. This macro will define them both for you, because they
//      don't really vary from event to event.
// Use this if you have a Windows.Foundation.TypedEventHandler
#define DEFINE_EVENT_WITH_TYPED_EVENT_HANDLER(className, name, eventHandler, sender, args)                                                        \
    winrt::event_token className::name(Windows::Foundation::TypedEventHandler<sender, args> const& handler) { return eventHandler.add(handler); } \
    void className::name(winrt::event_token const& token) noexcept { eventHandler.remove(token); }