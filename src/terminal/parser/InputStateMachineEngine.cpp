// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"

#include "stateMachine.hpp"
#include "InputStateMachineEngine.hpp"

#include "../../inc/unicode.hpp"
#include "ascii.hpp"

#ifdef BUILD_ONECORE_INTERACTIVITY
#include "../../interactivity/inc/VtApiRedirection.hpp"
#endif

using namespace Microsoft::Console::VirtualTerminal;

struct CsiToVkey
{
    CsiActionCodes action;
    short vkey;
};

static constexpr std::array<CsiToVkey, 10> s_csiMap = {
    CsiToVkey{ CsiActionCodes::ArrowUp, VK_UP },
    CsiToVkey{ CsiActionCodes::ArrowDown, VK_DOWN },
    CsiToVkey{ CsiActionCodes::ArrowRight, VK_RIGHT },
    CsiToVkey{ CsiActionCodes::ArrowLeft, VK_LEFT },
    CsiToVkey{ CsiActionCodes::Home, VK_HOME },
    CsiToVkey{ CsiActionCodes::End, VK_END },
    CsiToVkey{ CsiActionCodes::CSI_F1, VK_F1 },
    CsiToVkey{ CsiActionCodes::CSI_F2, VK_F2 },
    CsiToVkey{ CsiActionCodes::CSI_F3, VK_F3 },
    CsiToVkey{ CsiActionCodes::CSI_F4, VK_F4 }
};

static bool operator==(const CsiToVkey& pair, const CsiActionCodes code) noexcept
{
    return pair.action == code;
}

struct GenericToVkey
{
    GenericKeyIdentifiers identifier;
    short vkey;
};

static constexpr std::array<GenericToVkey, 14> s_genericMap = {
    GenericToVkey{ GenericKeyIdentifiers::GenericHome, VK_HOME },
    GenericToVkey{ GenericKeyIdentifiers::Insert, VK_INSERT },
    GenericToVkey{ GenericKeyIdentifiers::Delete, VK_DELETE },
    GenericToVkey{ GenericKeyIdentifiers::GenericEnd, VK_END },
    GenericToVkey{ GenericKeyIdentifiers::Prior, VK_PRIOR },
    GenericToVkey{ GenericKeyIdentifiers::Next, VK_NEXT },
    GenericToVkey{ GenericKeyIdentifiers::F5, VK_F5 },
    GenericToVkey{ GenericKeyIdentifiers::F6, VK_F6 },
    GenericToVkey{ GenericKeyIdentifiers::F7, VK_F7 },
    GenericToVkey{ GenericKeyIdentifiers::F8, VK_F8 },
    GenericToVkey{ GenericKeyIdentifiers::F9, VK_F9 },
    GenericToVkey{ GenericKeyIdentifiers::F10, VK_F10 },
    GenericToVkey{ GenericKeyIdentifiers::F11, VK_F11 },
    GenericToVkey{ GenericKeyIdentifiers::F12, VK_F12 },
};

static bool operator==(const GenericToVkey& pair, const GenericKeyIdentifiers identifier) noexcept
{
    return pair.identifier == identifier;
}

struct Ss3ToVkey
{
    Ss3ActionCodes action;
    short vkey;
};

static constexpr std::array<Ss3ToVkey, 4> s_ss3Map = {
    Ss3ToVkey{ Ss3ActionCodes::SS3_F1, VK_F1 },
    Ss3ToVkey{ Ss3ActionCodes::SS3_F2, VK_F2 },
    Ss3ToVkey{ Ss3ActionCodes::SS3_F3, VK_F3 },
    Ss3ToVkey{ Ss3ActionCodes::SS3_F4, VK_F4 },
};

static bool operator==(const Ss3ToVkey& pair, const Ss3ActionCodes code) noexcept
{
    return pair.action == code;
}

InputStateMachineEngine::InputStateMachineEngine(std::unique_ptr<IInteractDispatch> pDispatch) :
    InputStateMachineEngine(std::move(pDispatch), false)
{
}

InputStateMachineEngine::InputStateMachineEngine(std::unique_ptr<IInteractDispatch> pDispatch, const bool lookingForDSR) :
    _pDispatch(std::move(pDispatch)),
    _lookingForDSR(lookingForDSR),
    _pfnFlushToInputQueue(nullptr)
{
    THROW_HR_IF_NULL(E_INVALIDARG, _pDispatch.get());
}

// Method Description:
// - Triggers the Execute action to indicate that the listener should
//      immediately respond to a C0 control character.
// Arguments:
// - wch - Character to dispatch.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionExecute(const wchar_t wch)
{
    return _DoControlCharacter(wch, false);
}

// Routine Description:
// - Writes a control character into the buffer. Think characters like tab, backspace, etc.
// Arguments:
// - wch - The character to write
// - writeAlt - Pass in the alt-state information here as it's not embedded
// Return Value:
// - True if successfully generated and written. False otherwise.
bool InputStateMachineEngine::_DoControlCharacter(const wchar_t wch, const bool writeAlt)
{
    bool success = false;
    if (wch == UNICODE_ETX && !writeAlt)
    {
        // This is Ctrl+C, which is handled specially by the host.
        success = _pDispatch->WriteCtrlC();
    }
    else if (wch >= '\x0' && wch < '\x20')
    {
        // This is a C0 Control Character.
        // This should be translated as Ctrl+(wch+x40)
        wchar_t actualChar = wch;
        bool writeCtrl = true;

        short vkey = 0;
        DWORD modifierState = 0;

        switch (wch)
        {
        case L'\b':
            // Process Ctrl+Bksp to delete whole words
            actualChar = '\x7f';
            success = _GenerateKeyFromChar(actualChar, vkey, modifierState);
            modifierState = 0;
            break;
        case L'\r':
            writeCtrl = false;
            success = _GenerateKeyFromChar(wch, vkey, modifierState);
            modifierState = 0;
            break;
        case L'\x1b':
            // Translate escape as the ESC key, NOT C-[.
            // This means that C-[ won't insert ^[ into the buffer anymore,
            //      which isn't the worst tradeoff.
            vkey = VK_ESCAPE;
            writeCtrl = false;
            success = true;
            break;
        case L'\t':
            writeCtrl = false;
            success = _GenerateKeyFromChar(actualChar, vkey, modifierState);
            break;
        default:
            success = _GenerateKeyFromChar(actualChar, vkey, modifierState);
            break;
        }

        if (success)
        {
            if (writeCtrl)
            {
                WI_SetFlag(modifierState, LEFT_CTRL_PRESSED);
            }
            if (writeAlt)
            {
                WI_SetFlag(modifierState, LEFT_ALT_PRESSED);
            }

            success = _WriteSingleKey(actualChar, vkey, modifierState);
        }
    }
    else if (wch == '\x7f')
    {
        // Note:
        //  The windows telnet expects to send x7f as DELETE, not backspace.
        //      However, the windows telnetd also wouldn't let you move the
        //      cursor back into the input line, so it wasn't possible to
        //      "delete" any input at all, only backspace.
        //  Because of this, we're treating x7f as backspace, like most
        //      terminals do.
        success = _WriteSingleKey('\x8', VK_BACK, writeAlt ? LEFT_ALT_PRESSED : 0);
    }
    else
    {
        success = ActionPrint(wch);
    }
    return success;
}

// Routine Description:
// - Triggers the Execute action to indicate that the listener should
//      immediately respond to a C0 control character.
// This is called from the Escape state in the state machine, indicating the
//      immediately previous character was an 0x1b.
// We need to override this method to properly treat 0x1b + C0 strings as
//      Ctrl+Alt+<char> input sequences.
// Arguments:
// - wch - Character to dispatch.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionExecuteFromEscape(const wchar_t wch)
{
    if (_pDispatch->IsVtInputEnabled() && _pfnFlushToInputQueue)
    {
        return _pfnFlushToInputQueue();
    }

    return _DoControlCharacter(wch, true);
}

// Method Description:
// - Triggers the Print action to indicate that the listener should render the
//      character given.
// Arguments:
// - wch - Character to dispatch.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionPrint(const wchar_t wch)
{
    short vkey = 0;
    DWORD modifierState = 0;
    bool success = _GenerateKeyFromChar(wch, vkey, modifierState);
    if (success)
    {
        success = _WriteSingleKey(wch, vkey, modifierState);
    }
    return success;
}

// Method Description:
// - Triggers the Print action to indicate that the listener should render the
//      string of characters given.
// Arguments:
// - string - string to dispatch.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionPrintString(const std::wstring_view string)
{
    if (string.empty())
    {
        return true;
    }
    return _pDispatch->WriteString(string);
}

// Method Description:
// - Triggers the Print action to indicate that the listener should render the
//      string of characters given.
// Arguments:
// - string - string to dispatch.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionPassThroughString(const std::wstring_view string)
{
    if (_pDispatch->IsVtInputEnabled())
    {
        // Synthesize string into key events that we'll write to the buffer
        // similar to TerminalInput::_SendInputSequence
        if (!string.empty())
        {
            try
            {
                std::deque<std::unique_ptr<IInputEvent>> inputEvents;
                for (const auto& wch : string)
                {
                    inputEvents.push_back(std::make_unique<KeyEvent>(true, 1ui16, 0ui16, 0ui16, wch, 0));
                }
                return _pDispatch->WriteInput(inputEvents);
            }
            catch (...)
            {
                LOG_HR(wil::ResultFromCaughtException());
            }
        }
    }
    return ActionPrintString(string);
}

// Method Description:
// - Triggers the EscDispatch action to indicate that the listener should handle
//      a simple escape sequence. These sequences traditionally start with ESC
//      and a simple letter. No complicated parameters.
// Arguments:
// - wch - Character to dispatch.
// - intermediates - Intermediate characters in the sequence
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionEscDispatch(const wchar_t wch,
                                                const std::basic_string_view<wchar_t> /*intermediates*/)
{
    if (_pDispatch->IsVtInputEnabled() && _pfnFlushToInputQueue)
    {
        return _pfnFlushToInputQueue();
    }

    bool success = false;

    // 0x7f is DEL, which we treat effectively the same as a ctrl character.
    if (wch == 0x7f)
    {
        success = _DoControlCharacter(wch, true);
    }
    else
    {
        DWORD modifierState = 0;
        short vk = 0;
        success = _GenerateKeyFromChar(wch, vk, modifierState);
        if (success)
        {
            // Alt is definitely pressed in the esc+key case.
            modifierState = WI_SetFlag(modifierState, LEFT_ALT_PRESSED);

            success = _WriteSingleKey(wch, vk, modifierState);
        }
    }

    return success;
}

// Method Description:
// - Triggers the CsiDispatch action to indicate that the listener should handle
//      a control sequence. These sequences perform various API-type commands
//      that can include many parameters.
// Arguments:
// - wch - Character to dispatch.
// - intermediates - Intermediate characters in the sequence
// - parameters - set of numeric parameters collected while parsing the sequence.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionCsiDispatch(const wchar_t wch,
                                                const std::basic_string_view<wchar_t> intermediates,
                                                const std::basic_string_view<size_t> parameters)
{
    if (_pDispatch->IsVtInputEnabled() && _pfnFlushToInputQueue)
    {
        return _pfnFlushToInputQueue();
    }

    DWORD modifierState = 0;
    short vkey = 0;
    unsigned int function = 0;
    size_t col = 0;
    size_t row = 0;

    // This is all the args after the first arg, and the count of args not including the first one.
    const auto remainingArgs = parameters.size() > 1 ? parameters.substr(1) : std::basic_string_view<size_t>{};

    bool success = false;
    // Handle intermediate characters, if any
    if (!intermediates.empty())
    {
        switch (static_cast<CsiIntermediateCodes>(intermediates.at(0)))
        {
        case CsiIntermediateCodes::MOUSE_SGR:
        {
            DWORD buttonState = 0;
            DWORD eventFlags = 0;
            modifierState = _GetSGRMouseModifierState(parameters);
            success = _GetSGRXYPosition(parameters, row, col);

            // we need _UpdateSGRMouseButtonState() on the left side here because we _always_ should be updating our state
            // even if we failed to parse a portion of this sequence.
            success = _UpdateSGRMouseButtonState(wch, parameters, buttonState, eventFlags) && success;
            success = success && _WriteMouseEvent(col, row, buttonState, modifierState, eventFlags);
        }
        default:
            success = false;
            break;
        }
        return success;
    }
    switch (static_cast<CsiActionCodes>(wch))
    {
    case CsiActionCodes::Generic:
        modifierState = _GetGenericKeysModifierState(parameters);
        success = _GetGenericVkey(parameters, vkey);
        break;
    // case CsiActionCodes::DSR_DeviceStatusReportResponse:
    case CsiActionCodes::CSI_F3:
        // The F3 case is special - it shares a code with the DeviceStatusResponse.
        // If we're looking for that response, then do that, and break out.
        // Else, fall though to the _GetCursorKeysModifierState handler.
        if (_lookingForDSR)
        {
            success = true;
            success = _GetXYPosition(parameters, row, col);
            break;
        }
    case CsiActionCodes::ArrowUp:
    case CsiActionCodes::ArrowDown:
    case CsiActionCodes::ArrowRight:
    case CsiActionCodes::ArrowLeft:
    case CsiActionCodes::Home:
    case CsiActionCodes::End:
    case CsiActionCodes::CSI_F1:
    case CsiActionCodes::CSI_F2:
    case CsiActionCodes::CSI_F4:
        success = _GetCursorKeysVkey(wch, vkey);
        modifierState = _GetCursorKeysModifierState(parameters, static_cast<CsiActionCodes>(wch));
        break;
    case CsiActionCodes::CursorBackTab:
        modifierState = SHIFT_PRESSED;
        vkey = VK_TAB;
        success = true;
        break;
    case CsiActionCodes::DTTERM_WindowManipulation:
        success = _GetWindowManipulationType(parameters, function);
        break;
    default:
        success = false;
        break;
    }

    if (success)
    {
        switch (static_cast<CsiActionCodes>(wch))
        {
        // case CsiActionCodes::DSR_DeviceStatusReportResponse:
        case CsiActionCodes::CSI_F3:
            // The F3 case is special - it shares a code with the DeviceStatusResponse.
            // If we're looking for that response, then do that, and break out.
            // Else, fall though to the _GetCursorKeysModifierState handler.
            if (_lookingForDSR)
            {
                success = _pDispatch->MoveCursor(row, col);
                // Right now we're only looking for on initial cursor
                //      position response. After that, only look for F3.
                _lookingForDSR = false;
                break;
            }
            __fallthrough;
        case CsiActionCodes::Generic:
        case CsiActionCodes::ArrowUp:
        case CsiActionCodes::ArrowDown:
        case CsiActionCodes::ArrowRight:
        case CsiActionCodes::ArrowLeft:
        case CsiActionCodes::Home:
        case CsiActionCodes::End:
        case CsiActionCodes::CSI_F1:
        case CsiActionCodes::CSI_F2:
        case CsiActionCodes::CSI_F4:
        case CsiActionCodes::CursorBackTab:
            success = _WriteSingleKey(vkey, modifierState);
            break;
        case CsiActionCodes::DTTERM_WindowManipulation:
            success = _pDispatch->WindowManipulation(static_cast<DispatchTypes::WindowManipulationType>(function),
                                                     remainingArgs);
            break;
        default:
            success = false;
            break;
        }
    }

    return success;
}

// Routine Description:
// - Triggers the Ss3Dispatch action to indicate that the listener should handle
//      a control sequence. These sequences perform various API-type commands
//      that can include many parameters.
// Arguments:
// - wch - Character to dispatch.
// - parameters - set of numeric parameters collected while parsing the sequence.
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionSs3Dispatch(const wchar_t wch,
                                                const std::basic_string_view<size_t> /*parameters*/)
{
    if (_pDispatch->IsVtInputEnabled() && _pfnFlushToInputQueue)
    {
        return _pfnFlushToInputQueue();
    }

    // Ss3 sequence keys aren't modified.
    // When F1-F4 *are* modified, they're sent as CSI sequences, not SS3's.
    const DWORD modifierState = 0;
    short vkey = 0;

    bool success = _GetSs3KeysVkey(wch, vkey);

    if (success)
    {
        success = _WriteSingleKey(vkey, modifierState);
    }

    return success;
}

// Method Description:
// - Triggers the Clear action to indicate that the state machine should erase
//      all internal state.
// Arguments:
// - <none>
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionClear() noexcept
{
    return true;
}

// Method Description:
// - Triggers the Ignore action to indicate that the state machine should eat
//      this character and say nothing.
// Arguments:
// - <none>
// Return Value:
// - true iff we successfully dispatched the sequence.
bool InputStateMachineEngine::ActionIgnore() noexcept
{
    return true;
}

// Method Description:
// - Triggers the OscDispatch action to indicate that the listener should handle a control sequence.
//   These sequences perform various API-type commands that can include many parameters.
// Arguments:
// - wch - Character to dispatch. This will be a BEL or ST char.
// - parameter - identifier of the OSC action to perform
// - string - OSC string we've collected. NOT null terminated.
// Return Value:
// - true if we handled the dispatch.
bool InputStateMachineEngine::ActionOscDispatch(const wchar_t /*wch*/,
                                                const size_t /*parameter*/,
                                                const std::wstring_view /*string*/) noexcept
{
    return false;
}

// Method Description:
// - Writes a sequence of keypresses to the buffer based on the wch,
//      vkey and modifiers passed in. Will create both the appropriate key downs
//      and ups for that key for writing to the input. Will also generate
//      keypresses for pressing the modifier keys while typing that character.
//  If rgInput isn't big enough, then it will stop writing when it's filled.
// Arguments:
// - wch - the character to write to the input callback.
// - vkey - the VKEY of the key to write to the input callback.
// - modifierState - the modifier state to write with the key.
// - input - the buffer of characters to write the keypresses to. Can write
//      up to 8 records to this buffer.
// Return Value:
// - the number of records written, or 0 if the buffer wasn't big enough.
void InputStateMachineEngine::_GenerateWrappedSequence(const wchar_t wch,
                                                       const short vkey,
                                                       const DWORD modifierState,
                                                       std::vector<INPUT_RECORD>& input)
{
    input.reserve(input.size() + 8);

    // TODO: Reuse the clipboard functions for generating input for characters
    //       that aren't on the current keyboard.
    // MSFT:13994942

    const bool shift = WI_IsFlagSet(modifierState, SHIFT_PRESSED);
    const bool ctrl = WI_IsFlagSet(modifierState, LEFT_CTRL_PRESSED);
    const bool alt = WI_IsFlagSet(modifierState, LEFT_ALT_PRESSED);

    INPUT_RECORD next{ 0 };

    DWORD currentModifiers = 0;

    if (shift)
    {
        WI_SetFlag(currentModifiers, SHIFT_PRESSED);
        next.EventType = KEY_EVENT;
        next.Event.KeyEvent.bKeyDown = TRUE;
        next.Event.KeyEvent.dwControlKeyState = currentModifiers;
        next.Event.KeyEvent.wRepeatCount = 1;
        next.Event.KeyEvent.wVirtualKeyCode = VK_SHIFT;
        next.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(VK_SHIFT, MAPVK_VK_TO_VSC));
        next.Event.KeyEvent.uChar.UnicodeChar = 0x0;
        input.push_back(next);
    }
    if (alt)
    {
        WI_SetFlag(currentModifiers, LEFT_ALT_PRESSED);
        next.EventType = KEY_EVENT;
        next.Event.KeyEvent.bKeyDown = TRUE;
        next.Event.KeyEvent.dwControlKeyState = currentModifiers;
        next.Event.KeyEvent.wRepeatCount = 1;
        next.Event.KeyEvent.wVirtualKeyCode = VK_MENU;
        next.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC));
        next.Event.KeyEvent.uChar.UnicodeChar = 0x0;
        input.push_back(next);
    }
    if (ctrl)
    {
        WI_SetFlag(currentModifiers, LEFT_CTRL_PRESSED);
        next.EventType = KEY_EVENT;
        next.Event.KeyEvent.bKeyDown = TRUE;
        next.Event.KeyEvent.dwControlKeyState = currentModifiers;
        next.Event.KeyEvent.wRepeatCount = 1;
        next.Event.KeyEvent.wVirtualKeyCode = VK_CONTROL;
        next.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC));
        next.Event.KeyEvent.uChar.UnicodeChar = 0x0;
        input.push_back(next);
    }

    // Use modifierState instead of currentModifiers here.
    // This allows other modifiers like ENHANCED_KEY to get
    //    through on the KeyPress.
    _GetSingleKeypress(wch, vkey, modifierState, input);

    if (ctrl)
    {
        WI_ClearFlag(currentModifiers, LEFT_CTRL_PRESSED);
        next.EventType = KEY_EVENT;
        next.Event.KeyEvent.bKeyDown = FALSE;
        next.Event.KeyEvent.dwControlKeyState = currentModifiers;
        next.Event.KeyEvent.wRepeatCount = 1;
        next.Event.KeyEvent.wVirtualKeyCode = VK_CONTROL;
        next.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC));
        next.Event.KeyEvent.uChar.UnicodeChar = 0x0;
        input.push_back(next);
    }
    if (alt)
    {
        WI_ClearFlag(currentModifiers, LEFT_ALT_PRESSED);
        next.EventType = KEY_EVENT;
        next.Event.KeyEvent.bKeyDown = FALSE;
        next.Event.KeyEvent.dwControlKeyState = currentModifiers;
        next.Event.KeyEvent.wRepeatCount = 1;
        next.Event.KeyEvent.wVirtualKeyCode = VK_MENU;
        next.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC));
        next.Event.KeyEvent.uChar.UnicodeChar = 0x0;
        input.push_back(next);
    }
    if (shift)
    {
        WI_ClearFlag(currentModifiers, SHIFT_PRESSED);
        next.EventType = KEY_EVENT;
        next.Event.KeyEvent.bKeyDown = FALSE;
        next.Event.KeyEvent.dwControlKeyState = currentModifiers;
        next.Event.KeyEvent.wRepeatCount = 1;
        next.Event.KeyEvent.wVirtualKeyCode = VK_SHIFT;
        next.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(VK_SHIFT, MAPVK_VK_TO_VSC));
        next.Event.KeyEvent.uChar.UnicodeChar = 0x0;
        input.push_back(next);
    }
}

// Method Description:
// - Writes a single character keypress to the input buffer. This writes both
//      the keydown and keyup events.
// Arguments:
// - wch - the character to write to the buffer.
// - vkey - the VKEY of the key to write to the buffer.
// - modifierState - the modifier state to write with the key.
// - input - the buffer of characters to write the keypress to. Will always
//      write to the first two positions in the buffer.
// Return Value:
// - the number of input records written.
void InputStateMachineEngine::_GetSingleKeypress(const wchar_t wch,
                                                 const short vkey,
                                                 const DWORD modifierState,
                                                 std::vector<INPUT_RECORD>& input)
{
    input.reserve(input.size() + 2);

    INPUT_RECORD rec;

    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.dwControlKeyState = modifierState;
    rec.Event.KeyEvent.wRepeatCount = 1;
    rec.Event.KeyEvent.wVirtualKeyCode = vkey;
    rec.Event.KeyEvent.wVirtualScanCode = gsl::narrow_cast<WORD>(MapVirtualKey(vkey, MAPVK_VK_TO_VSC));
    rec.Event.KeyEvent.uChar.UnicodeChar = wch;
    input.push_back(rec);

    rec.Event.KeyEvent.bKeyDown = FALSE;
    input.push_back(rec);
}

// Method Description:
// - Writes a sequence of keypresses to the input callback based on the wch,
//      vkey and modifiers passed in. Will create both the appropriate key downs
//      and ups for that key for writing to the input. Will also generate
//      keypresses for pressing the modifier keys while typing that character.
// Arguments:
// - wch - the character to write to the input callback.
// - vkey - the VKEY of the key to write to the input callback.
// - modifierState - the modifier state to write with the key.
// Return Value:
// - true iff we successfully wrote the keypress to the input callback.
bool InputStateMachineEngine::_WriteSingleKey(const wchar_t wch, const short vkey, const DWORD modifierState)
{
    // At most 8 records - 2 for each of shift,ctrl,alt up and down, and 2 for the actual key up and down.
    std::vector<INPUT_RECORD> input;
    _GenerateWrappedSequence(wch, vkey, modifierState, input);
    std::deque<std::unique_ptr<IInputEvent>> inputEvents = IInputEvent::Create(gsl::make_span(input));

    return _pDispatch->WriteInput(inputEvents);
}

// Method Description:
// - Helper for writing a single key to the input when you only know the vkey.
//      Will automatically get the wchar_t associated with that vkey.
// Arguments:
// - vkey - the VKEY of the key to write to the input callback.
// - modifierState - the modifier state to write with the key.
// Return Value:
// - true iff we successfully wrote the keypress to the input callback.
bool InputStateMachineEngine::_WriteSingleKey(const short vkey, const DWORD modifierState)
{
    const wchar_t wch = gsl::narrow_cast<wchar_t>(MapVirtualKey(vkey, MAPVK_VK_TO_CHAR));
    return _WriteSingleKey(wch, vkey, modifierState);
}

// Method Description:
// - Writes a Mouse Event Record to the input callback based on the state of the mouse.
// Arguments:
// - column - the X/Column position on the viewport (0 = left-most)
// - line - the Y/Line/Row position on the viewport (0 = top-most)
// - buttonState - the mouse buttons that are being modified
// - modifierState - the modifier state to write mouse record.
// - eventFlags - the type of mouse event to write to the mouse record.
// Return Value:
// - true iff we successfully wrote the keypress to the input callback.
bool InputStateMachineEngine::_WriteMouseEvent(const size_t column, const size_t line, const DWORD buttonState, const DWORD controlKeyState, const DWORD eventFlags)
{
    COORD uiPos = { gsl::narrow<short>(column) - 1, gsl::narrow<short>(line) - 1 };

    INPUT_RECORD rgInput;
    rgInput.EventType = MOUSE_EVENT;
    rgInput.Event.MouseEvent.dwMousePosition = uiPos;
    rgInput.Event.MouseEvent.dwButtonState = buttonState;
    rgInput.Event.MouseEvent.dwControlKeyState = controlKeyState;
    rgInput.Event.MouseEvent.dwEventFlags = eventFlags;

    // pack and write input record
    // 1 record - the modifiers don't get their own events
    std::deque<std::unique_ptr<IInputEvent>> inputEvents = IInputEvent::Create(gsl::make_span(&rgInput, 1));
    return _pDispatch->WriteInput(inputEvents);
}

// Method Description:
// - Retrieves the modifier state from a set of parameters for a cursor keys
//      sequence. This is for Arrow keys, Home, End, etc.
// Arguments:
// - parameters - the set of parameters to get the modifier state from.
// - actionCode - the actionCode for the sequence we're operating on.
// Return Value:
// - the INPUT_RECORD compatible modifier state.
DWORD InputStateMachineEngine::_GetCursorKeysModifierState(const std::basic_string_view<size_t> parameters, const CsiActionCodes actionCode) noexcept
{
    DWORD modifiers = 0;
    if (_IsModified(parameters.size()) && parameters.size() >= 2)
    {
        modifiers = _GetModifier(parameters.at(1));
    }

    // Enhanced Keys (from https://docs.microsoft.com/en-us/windows/console/key-event-record-str):
    //   Enhanced keys for the IBM 101- and 102-key keyboards are the INS, DEL,
    //   HOME, END, PAGE UP, PAGE DOWN, and direction keys in the clusters to the left
    //   of the keypad; and the divide (/) and ENTER keys in the keypad.
    // This snippet detects the direction keys + HOME + END
    // actionCode should be one of the above, so just make sure it's not a CSI_F# code
    if (actionCode < CsiActionCodes::CSI_F1 || actionCode > CsiActionCodes::CSI_F4)
    {
        WI_SetFlag(modifiers, ENHANCED_KEY);
    }

    return modifiers;
}

// Method Description:
// - Retrieves the modifier state from a set of parameters for a "Generic"
//      keypress - one who's sequence is terminated with a '~'.
// Arguments:
// - parameters - the set of parameters to get the modifier state from.
// Return Value:
// - the INPUT_RECORD compatible modifier state.
DWORD InputStateMachineEngine::_GetGenericKeysModifierState(const std::basic_string_view<size_t> parameters) noexcept
{
    DWORD modifiers = 0;
    if (_IsModified(parameters.size()) && parameters.size() >= 2)
    {
        modifiers = _GetModifier(parameters.at(1));
    }

    // Enhanced Keys (from https://docs.microsoft.com/en-us/windows/console/key-event-record-str):
    //   Enhanced keys for the IBM 101- and 102-key keyboards are the INS, DEL,
    //   HOME, END, PAGE UP, PAGE DOWN, and direction keys in the clusters to the left
    //   of the keypad; and the divide (/) and ENTER keys in the keypad.
    // This snippet detects the non-direction keys
    const auto identifier = static_cast<GenericKeyIdentifiers>(til::at(parameters, 0));
    if (identifier <= GenericKeyIdentifiers::Next)
    {
        modifiers = WI_SetFlag(modifiers, ENHANCED_KEY);
    }

    return modifiers;
}

// Method Description:
// - Retrieves the modifier state from a set of parameters for an SGR
//      Mouse Sequence - one who's sequence is terminated with an 'M' or 'm'.
// Arguments:
// - parameters - the set of parameters to get the modifier state from.
// Return Value:
// - the INPUT_RECORD compatible modifier state.
DWORD InputStateMachineEngine::_GetSGRMouseModifierState(const std::basic_string_view<size_t> parameters) noexcept
{
    DWORD modifiers = 0;
    if (parameters.size() == 3)
    {
        // The first parameter of mouse events is encoded as the following two bytes:
        // BBDM'MMBB
        // Where each of the bits mean the following
        //   BB__'__BB - which button was pressed/released
        //   MMM - Control, Alt, Shift state (respectively)
        //   D - flag signifying a drag event
        // This retrieves the modifier state from bits [5..3] ('M' above)
        const auto modifierParam = til::at(parameters, 0);
        WI_SetFlagIf(modifiers, SHIFT_PRESSED, WI_IsFlagSet(modifierParam, CsiMouseModifierCodes::Shift));
        WI_SetFlagIf(modifiers, LEFT_ALT_PRESSED, WI_IsFlagSet(modifierParam, CsiMouseModifierCodes::Meta));
        WI_SetFlagIf(modifiers, LEFT_CTRL_PRESSED, WI_IsFlagSet(modifierParam, CsiMouseModifierCodes::Ctrl));
    }
    return modifiers;
}

// Method Description:
// - Determines if a set of parameters indicates a modified keypress
// Arguments:
// - paramCount - the number of parameters we've collected in this sequence
// Return Value:
// - true iff the sequence is a modified sequence.
bool InputStateMachineEngine::_IsModified(const size_t paramCount) noexcept
{
    // modified input either looks like
    // \x1b[1;mA or \x1b[17;m~
    // Both have two parameters
    return paramCount == 2;
}

// Method Description:
// - Converts a VT encoded modifier param into a INPUT_RECORD compatible one.
// Arguments:
// - modifierParam - the VT modifier value to convert
// Return Value:
// - The equivalent INPUT_RECORD modifier value.
DWORD InputStateMachineEngine::_GetModifier(const size_t modifierParam) noexcept
{
    // VT Modifiers are 1+(modifier flags)
    const auto vtParam = modifierParam - 1;
    DWORD modifierState = 0;
    WI_SetFlagIf(modifierState, ENHANCED_KEY, modifierParam > 0);
    WI_SetFlagIf(modifierState, SHIFT_PRESSED, WI_IsFlagSet(vtParam, VT_SHIFT));
    WI_SetFlagIf(modifierState, LEFT_ALT_PRESSED, WI_IsFlagSet(vtParam, VT_ALT));
    WI_SetFlagIf(modifierState, LEFT_CTRL_PRESSED, WI_IsFlagSet(vtParam, VT_CTRL));
    return modifierState;
}

// Method Description:
// - Synthesize the button state for the Mouse Input Record from an SGR VT Sequence
// - Here, we refer to and maintain the global state of our mouse.
// - Mouse wheel events are added at the end to keep them out of the global state
// Arguments:
// - wch: the wchar_t representing whether the button was pressed or released
// - parameters: the wchar_t to get the mapped vkey of. Represents the direction of the button (down vs up)
// - buttonState: Receives the button state for the record
// - eventFlags: Receives the special mouse events for the record
// Return Value:
// true iff we were able to synthesize buttonState
bool InputStateMachineEngine::_UpdateSGRMouseButtonState(const wchar_t wch,
                                                         const std::basic_string_view<size_t> parameters,
                                                         DWORD& buttonState,
                                                         DWORD& eventFlags) noexcept
{
    if (parameters.empty())
    {
        return false;
    }

    // Starting with the state from the last mouse event we received
    buttonState = _mouseButtonState;
    eventFlags = 0;

    // The first parameter of mouse events is encoded as the following two bytes:
    // BBDM'MMBB
    // Where each of the bits mean the following
    //   BB__'__BB - which button was pressed/released
    //   MMM - Control, Alt, Shift state (respectively)
    //   D - flag signifying a drag event
    const auto sgrEncoding = til::at(parameters, 0);

    // This retrieves the 2 MSBs and concatenates them to the 2 LSBs to create BBBB in binary
    // This represents which button had a change in state
    const auto buttonID = (sgrEncoding & 0x3) | ((sgrEncoding & 0xC0) >> 4);

    // Step 1: Translate which button was affected
    // NOTE: if scrolled, having buttonFlag = 0 means
    //       we don't actually update the buttonState
    DWORD buttonFlag = 0;
    switch (buttonID)
    {
    case CsiMouseButtonCodes::Left:
        buttonFlag = FROM_LEFT_1ST_BUTTON_PRESSED;
        break;
    case CsiMouseButtonCodes::Right:
        buttonFlag = RIGHTMOST_BUTTON_PRESSED;
        break;
    case CsiMouseButtonCodes::Middle:
        buttonFlag = FROM_LEFT_2ND_BUTTON_PRESSED;
        break;
    case CsiMouseButtonCodes::ScrollBack:
    {
        // set high word to proper scroll direction
        // scroll intensity is assumed to be constant value
        buttonState |= SCROLL_DELTA_BACKWARD;
        eventFlags |= MOUSE_WHEELED;
        break;
    }
    case CsiMouseButtonCodes::ScrollForward:
    {
        // set high word to proper scroll direction
        // scroll intensity is assumed to be constant value
        buttonState |= SCROLL_DELTA_FORWARD;
        eventFlags |= MOUSE_WHEELED;
        break;
    }
    default:
        // no detectable buttonID, so we can't update the state
        return false;
    }

    // Step 2: Decide whether to set or clear that button's bit
    // NOTE: WI_SetFlag/WI_ClearFlag can't be used here because buttonFlag would have to be a compile-time constant
    switch (static_cast<CsiActionCodes>(wch))
    {
    case CsiActionCodes::MouseDown:
        // set flag
        // NOTE: scroll events have buttonFlag = 0
        //       so this intentionally does nothing
        buttonState |= buttonFlag;
        break;
    case CsiActionCodes::MouseUp:
        // clear flag
        buttonState &= (~buttonFlag);
        break;
    default:
        // no detectable change of state, so we can't update the state
        return false;
    }

    // Step 3: check if mouse moved
    if (WI_IsFlagSet(sgrEncoding, CsiMouseModifierCodes::Drag))
    {
        eventFlags |= MOUSE_MOVED;
    }

    // Step 4: update internal state before returning, even if we couldn't fully understand this
    // only take LOWORD here because HIWORD is reserved for mouse wheel delta and release events for the wheel buttons are not reported
    _mouseButtonState = LOWORD(buttonState);

    return true;
}

// Method Description:
// - Gets the Vkey form the generic keys table associated with a particular
//   identifier code. The identifier code will be the first param in rgusParams.
// Arguments:
// - parameters: an array of shorts where the first is the identifier of the key
//      we're looking for.
// - vkey: Receives the vkey
// Return Value:
// true iff we found the key
bool InputStateMachineEngine::_GetGenericVkey(const std::basic_string_view<size_t> parameters, short& vkey) const
{
    vkey = 0;
    if (parameters.empty())
    {
        return false;
    }

    const auto identifier = (GenericKeyIdentifiers)til::at(parameters, 0);

    const auto mapping = std::find(s_genericMap.cbegin(), s_genericMap.cend(), identifier);
    if (mapping != s_genericMap.end())
    {
        vkey = mapping->vkey;
        return true;
    }

    return false;
}

// Method Description:
// - Gets the Vkey from the CSI codes table associated with a particular character.
// Arguments:
// - wch: the wchar_t to get the mapped vkey of.
// - vkey: Receives the vkey
// Return Value:
// true iff we found the key
bool InputStateMachineEngine::_GetCursorKeysVkey(const wchar_t wch, short& vkey) const
{
    vkey = 0;

    const auto mapping = std::find(s_csiMap.cbegin(), s_csiMap.cend(), (CsiActionCodes)wch);
    if (mapping != s_csiMap.end())
    {
        vkey = mapping->vkey;
        return true;
    }

    return false;
}

// Method Description:
// - Gets the Vkey from the SS3 codes table associated with a particular character.
// Arguments:
// - wch: the wchar_t to get the mapped vkey of.
// - pVkey: Receives the vkey
// Return Value:
// true iff we found the key
bool InputStateMachineEngine::_GetSs3KeysVkey(const wchar_t wch, short& vkey) const
{
    vkey = 0;

    const auto mapping = std::find(s_ss3Map.cbegin(), s_ss3Map.cend(), (Ss3ActionCodes)wch);
    if (mapping != s_ss3Map.end())
    {
        vkey = mapping->vkey;
        return true;
    }

    return false;
}

// Method Description:
// - Gets the Vkey and modifier state that's associated with a particular char.
// Arguments:
// - wch: the wchar_t to get the vkey and modifier state of.
// - vkey: Receives the vkey
// - modifierState: Receives the modifier state
// Return Value:
// <none>
bool InputStateMachineEngine::_GenerateKeyFromChar(const wchar_t wch,
                                                   short& vkey,
                                                   DWORD& modifierState) noexcept
{
    // Low order byte is key, high order is modifiers
    const short keyscan = VkKeyScanW(wch);

    short key = LOBYTE(keyscan);

    const short keyscanModifiers = HIBYTE(keyscan);

    if (key == -1 && keyscanModifiers == -1)
    {
        return false;
    }

    // Because of course, these are not the same flags.
    short modifierFlags = 0 |
                          (WI_IsFlagSet(keyscanModifiers, KEYSCAN_SHIFT) ? SHIFT_PRESSED : 0) |
                          (WI_IsFlagSet(keyscanModifiers, KEYSCAN_CTRL) ? LEFT_CTRL_PRESSED : 0) |
                          (WI_IsFlagSet(keyscanModifiers, KEYSCAN_ALT) ? LEFT_ALT_PRESSED : 0);

    vkey = key;
    modifierState = modifierFlags;

    return true;
}

// Method Description:
// - Returns true if the engine should dispatch on the last character of a string
//      always, even if the sequence hasn't normally dispatched.
//   If this is false, the engine will persist its state across calls to
//      ProcessString, and dispatch only at the end of the sequence.
// Return Value:
// - True iff we should manually dispatch on the last character of a string.
bool InputStateMachineEngine::FlushAtEndOfString() const noexcept
{
    return true;
}

// Routine Description:
// - Returns true if the engine should dispatch control characters in the Escape
//      state. Typically, control characters are immediately executed in the
//      Escape state without returning to ground. If this returns true, the
//      state machine will instead call ActionExecuteFromEscape and then enter
//      the Ground state when a control character is encountered in the escape
//      state.
// Return Value:
// - True iff we should return to the Ground state when the state machine
//      encounters a Control (C0) character in the Escape state.
bool InputStateMachineEngine::DispatchControlCharsFromEscape() const noexcept
{
    return true;
}

// Routine Description:
// - Returns false if the engine wants to be able to collect intermediate
//   characters in the Escape state. We do _not_ want to buffer any characters
//   as intermediates, because we use ESC as a prefix to indicate a key was
//   pressed while Alt was pressed.
// Return Value:
// - True iff we should dispatch in the Escape state when we encounter a
//   Intermediate character.
bool InputStateMachineEngine::DispatchIntermediatesFromEscape() const noexcept
{
    return true;
}

// Method Description:
// - Sets us up for vt input passthrough.
//      We'll set a couple members, and if they aren't null, when we get a
//      sequence we don't understand, we'll pass it along to the app
//      instead of eating it ourselves.
// Arguments:
// - pfnFlushToInputQueue: This is a callback to the underlying state machine to
//      trigger it to call ActionPassThroughString with whatever sequence it's
//      currently processing.
// Return Value:
// - <none>
void InputStateMachineEngine::SetFlushToInputQueueCallback(std::function<bool()> pfnFlushToInputQueue)
{
    _pfnFlushToInputQueue = pfnFlushToInputQueue;
}

// Method Description:
// - Retrieves the type of window manipulation operation from the parameter pool
//      stored during Param actions.
//  This is kept separate from the output version, as there may be
//      codes that are supported in one direction but not the other.
// Arguments:
// - parameters - Array of parameters collected
// - function - Receives the function type
// Return Value:
// - True iff we successfully pulled the function type from the parameters
bool InputStateMachineEngine::_GetWindowManipulationType(const std::basic_string_view<size_t> parameters,
                                                         unsigned int& function) const noexcept
{
    bool success = false;
    function = DispatchTypes::WindowManipulationType::Invalid;

    if (!parameters.empty())
    {
        switch (til::at(parameters, 0))
        {
        case DispatchTypes::WindowManipulationType::RefreshWindow:
            function = DispatchTypes::WindowManipulationType::RefreshWindow;
            success = true;
            break;
        case DispatchTypes::WindowManipulationType::ResizeWindowInCharacters:
            function = DispatchTypes::WindowManipulationType::ResizeWindowInCharacters;
            success = true;
            break;
        default:
            success = false;
        }
    }

    return success;
}

// Routine Description:
// - Retrieves an X/Y coordinate pair for a cursor operation from the parameter pool stored during Param actions.
// Arguments:
// - parameters - set of numeric parameters collected while parsing the sequence.
// - line - Receives the Y/Line/Row position
// - column - Receives the X/Column position
// Return Value:
// - True if we successfully pulled the cursor coordinates from the parameters we've stored. False otherwise.
bool InputStateMachineEngine::_GetXYPosition(const std::basic_string_view<size_t> parameters,
                                             size_t& line,
                                             size_t& column) const noexcept
{
    line = DefaultLine;
    column = DefaultColumn;

    if (parameters.empty())
    {
        // Empty parameter sequences should use the default
    }
    else if (parameters.size() == 1)
    {
        // If there's only one param, leave the default for the column, and retrieve the specified row.
        line = til::at(parameters, 0);
    }
    else if (parameters.size() == 2)
    {
        // If there are exactly two parameters, use them.
        line = til::at(parameters, 0);
        column = til::at(parameters, 1);
    }
    else
    {
        return false;
    }

    // Distances of 0 should be changed to 1.
    if (line == 0)
    {
        line = DefaultLine;
    }

    if (column == 0)
    {
        column = DefaultColumn;
    }

    return true;
}

// Routine Description:
// - Retrieves an X/Y coordinate pair for an SGR Mouse sequence from the parameter pool stored during Param actions.
// Arguments:
// - parameters - set of numeric parameters collected while parsing the sequence.
// - line - Receives the Y/Line/Row position
// - column - Receives the X/Column position
// Return Value:
// - True if we successfully pulled the cursor coordinates from the parameters we've stored. False otherwise.
bool InputStateMachineEngine::_GetSGRXYPosition(const std::basic_string_view<size_t> parameters,
                                                size_t& line,
                                                size_t& column) const noexcept
{
    line = DefaultLine;
    column = DefaultColumn;

    // SGR Mouse sequences have exactly 3 parameters
    if (parameters.size() == 3)
    {
        column = til::at(parameters, 1);
        line = til::at(parameters, 2);
    }
    else
    {
        return false;
    }

    // Distances of 0 should be changed to 1.
    if (line == 0)
    {
        line = DefaultLine;
    }

    if (column == 0)
    {
        column = DefaultColumn;
    }

    return true;
}
