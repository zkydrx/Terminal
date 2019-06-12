// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "UiaTextRange.hpp"
#include "../inc/ServiceLocator.hpp"

#include "window.hpp"
#include "windowdpiapi.hpp"
#include "../host/tracing.hpp"

#include "../host/selection.hpp"
#include "../host/search.h"

using namespace Microsoft::Console::Interactivity::Win32;
using namespace Microsoft::Console::Interactivity::Win32::UiaTextRangeTracing;
using namespace Microsoft::Console::Interactivity;

// toggle these for additional logging in a debug build
//#define UIATEXTRANGE_DEBUG_MSGS 1
#undef UIATEXTRANGE_DEBUG_MSGS

IdType UiaTextRange::id = 1;

UiaTextRange::MoveState::MoveState(const UiaTextRange& range,
                                   const MovementDirection direction) :
    StartScreenInfoRow{ UiaTextRange::_endpointToScreenInfoRow(range.GetStart()) },
    StartColumn{ UiaTextRange::_endpointToColumn(range.GetStart()) },
    EndScreenInfoRow{ UiaTextRange::_endpointToScreenInfoRow(range.GetEnd()) },
    EndColumn{ UiaTextRange::_endpointToColumn(range.GetEnd()) },
    Direction{ direction }
{
    if (direction == MovementDirection::Forward)
    {
        LimitingRow = UiaTextRange::_getLastScreenInfoRowIndex();
        FirstColumnInRow = UiaTextRange::_getFirstColumnIndex();
        LastColumnInRow = UiaTextRange::_getLastColumnIndex();
        Increment = MovementIncrement::Forward;
    }
    else
    {
        LimitingRow = UiaTextRange::_getFirstScreenInfoRowIndex();
        FirstColumnInRow = UiaTextRange::_getLastColumnIndex();
        LastColumnInRow = UiaTextRange::_getFirstColumnIndex();
        Increment = MovementIncrement::Backward;
    }
}

UiaTextRange::MoveState::MoveState(const ScreenInfoRow startScreenInfoRow,
                                   const Column startColumn,
                                   const ScreenInfoRow endScreenInfoRow,
                                   const Column endColumn,
                                   const ScreenInfoRow limitingRow,
                                   const Column firstColumnInRow,
                                   const Column lastColumnInRow,
                                   const MovementIncrement increment,
                                   const MovementDirection direction) :
    StartScreenInfoRow{ startScreenInfoRow },
    StartColumn{ startColumn },
    EndScreenInfoRow{ endScreenInfoRow },
    EndColumn{ endColumn },
    LimitingRow{ limitingRow },
    FirstColumnInRow{ firstColumnInRow },
    LastColumnInRow{ lastColumnInRow },
    Increment{ increment },
    Direction{ direction }
{
}

#if _DEBUG
#include <sstream>
// This is a debugging function that prints out the current
// relationship between screen info rows, text buffer rows, and
// endpoints.
void UiaTextRange::_outputRowConversions()
{
    try
    {
        unsigned int totalRows = _getTotalRows();
        OutputDebugString(L"screenBuffer\ttextBuffer\tendpoint\n");
        for (unsigned int i = 0; i < totalRows; ++i)
        {
            std::wstringstream ss;
            ss << i << "\t" << _screenInfoRowToTextBufferRow(i) << "\t" << _screenInfoRowToEndpoint(i) << "\n";
            std::wstring str = ss.str();
            OutputDebugString(str.c_str());
        }
        OutputDebugString(L"\n");
    }
    catch (...)
    {
        LOG_HR(wil::ResultFromCaughtException());
    }
}

void UiaTextRange::_outputObjectState()
{
    std::wstringstream ss;
    ss << "Object State";
    ss << " _id: " << _id;
    ss << " _start: " << _start;
    ss << " _end: " << _end;
    ss << " _degenerate: " << _degenerate;

    std::wstring str = ss.str();
    OutputDebugString(str.c_str());
    OutputDebugString(L"\n");
}
#endif // _DEBUG

std::deque<UiaTextRange*> UiaTextRange::GetSelectionRanges(_In_ IRawElementProviderSimple* pProvider)
{
    std::deque<UiaTextRange*> ranges;

    // get the selection rects
    const auto rectangles = Selection::Instance().GetSelectionRects();

    // create a range for each row
    for (const auto& rect : rectangles)
    {
        ScreenInfoRow currentRow = rect.Top;
        Endpoint start = _screenInfoRowToEndpoint(currentRow) + rect.Left;
        Endpoint end = _screenInfoRowToEndpoint(currentRow) + rect.Right;
        UiaTextRange* range = UiaTextRange::Create(pProvider,
                                                   start,
                                                   end,
                                                   false);
        if (range == nullptr)
        {
            // something when wrong, clean up and throw
            while (!ranges.empty())
            {
                UiaTextRange* temp = ranges[0];
                ranges.pop_front();
                temp->Release();
            }
            throw E_INVALIDARG;
        }
        else
        {
            ranges.push_back(range);
        }
    }
    return ranges;
}

UiaTextRange* UiaTextRange::Create(_In_ IRawElementProviderSimple* const pProvider)
{
    UiaTextRange* range = nullptr;
    ;
    try
    {
        range = new UiaTextRange(pProvider);
    }
    catch (...)
    {
        range = nullptr;
    }

    if (range)
    {
        pProvider->AddRef();
    }
    return range;
}

UiaTextRange* UiaTextRange::Create(_In_ IRawElementProviderSimple* const pProvider,
                                   const Cursor& cursor)
{
    UiaTextRange* range = nullptr;
    try
    {
        range = new UiaTextRange(pProvider, cursor);
    }
    catch (...)
    {
        range = nullptr;
    }

    if (range)
    {
        pProvider->AddRef();
    }
    return range;
}

UiaTextRange* UiaTextRange::Create(_In_ IRawElementProviderSimple* const pProvider,
                                   const Endpoint start,
                                   const Endpoint end,
                                   const bool degenerate)
{
    UiaTextRange* range = nullptr;
    try
    {
        range = new UiaTextRange(pProvider,
                                 start,
                                 end,
                                 degenerate);
    }
    catch (...)
    {
        range = nullptr;
    }

    if (range)
    {
        pProvider->AddRef();
    }
    return range;
}

UiaTextRange* UiaTextRange::Create(_In_ IRawElementProviderSimple* const pProvider,
                                   const UiaPoint point)
{
    UiaTextRange* range = nullptr;
    try
    {
        range = new UiaTextRange(pProvider, point);
    }
    catch (...)
    {
        range = nullptr;
    }

    if (range)
    {
        pProvider->AddRef();
    }
    return range;
}

// degenerate range constructor.
UiaTextRange::UiaTextRange(_In_ IRawElementProviderSimple* const pProvider) :
    _cRefs{ 1 },
    _pProvider{ THROW_HR_IF_NULL(E_INVALIDARG, pProvider) },
    _start{ 0 },
    _end{ 0 },
    _degenerate{ true }
{
    _id = id;
    ++id;

    // tracing
    ApiMsgConstructor apiMsg;
    apiMsg.Id = _id;
    Tracing::s_TraceUia(nullptr, ApiCall::Constructor, &apiMsg);
}

UiaTextRange::UiaTextRange(_In_ IRawElementProviderSimple* const pProvider,
                           const Cursor& cursor) :
    UiaTextRange(pProvider)
{
    _degenerate = true;
    _start = _screenInfoRowToEndpoint(cursor.GetPosition().Y) + cursor.GetPosition().X;
    _end = _start;

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"Constructor\n");
    _outputObjectState();
#endif
}

UiaTextRange::UiaTextRange(_In_ IRawElementProviderSimple* const pProvider,
                           const Endpoint start,
                           const Endpoint end,
                           const bool degenerate) :
    UiaTextRange(pProvider)
{
    THROW_HR_IF(E_INVALIDARG, !degenerate && start > end);

    _degenerate = degenerate;
    _start = start;
    _end = degenerate ? start : end;

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"Constructor\n");
    _outputObjectState();
#endif
}

// returns a degenerate text range of the start of the row closest to the y value of point
UiaTextRange::UiaTextRange(_In_ IRawElementProviderSimple* const pProvider,
                           const UiaPoint point) :
    UiaTextRange(pProvider)
{
    POINT clientPoint;
    clientPoint.x = static_cast<LONG>(point.x);
    clientPoint.y = static_cast<LONG>(point.y);
    // get row that point resides in
    const IConsoleWindow* const pIConsoleWindow = _getIConsoleWindow();
    const Window* const pWindow = static_cast<const Window* const>(pIConsoleWindow);
    const RECT windowRect = pWindow->GetWindowRect();
    const SMALL_RECT viewport = _getViewport().ToInclusive();
    ScreenInfoRow row;
    if (clientPoint.y <= windowRect.top)
    {
        row = viewport.Top;
    }
    else if (clientPoint.y >= windowRect.bottom)
    {
        row = viewport.Bottom;
    }
    else
    {
        // change point coords to pixels relative to window
        HWND hwnd = _getWindowHandle();
        ScreenToClient(hwnd, &clientPoint);

        const SCREEN_INFORMATION& _pScreenInfo = _getScreenInfo();
        const COORD currentFontSize = _pScreenInfo.GetScreenFontSize();
        row = (clientPoint.y / currentFontSize.Y) + viewport.Top;
    }
    _start = _screenInfoRowToEndpoint(row);
    _end = _start;
    _degenerate = true;

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"Constructor\n");
    _outputObjectState();
#endif
}

UiaTextRange::UiaTextRange(const UiaTextRange& a) :
    _cRefs{ 1 },
    _pProvider{ a._pProvider },
    _start{ a._start },
    _end{ a._end },
    _degenerate{ a._degenerate }
{
    (static_cast<IUnknown*>(_pProvider))->AddRef();
    _id = id;
    ++id;

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"Copy Constructor\n");
    _outputObjectState();
#endif
}

UiaTextRange::~UiaTextRange()
{
    (static_cast<IUnknown*>(_pProvider))->Release();
}

const IdType UiaTextRange::GetId() const
{
    return _id;
}

const Endpoint UiaTextRange::GetStart() const
{
    return _start;
}

const Endpoint UiaTextRange::GetEnd() const
{
    return _end;
}

// Routine Description:
// - returns true if the range is currently degenerate (empty range).
// Arguments:
// - <none>
// Return Value:
// - true if range is degenerate, false otherwise.
const bool UiaTextRange::IsDegenerate() const
{
    return _degenerate;
}

#pragma region IUnknown

IFACEMETHODIMP_(ULONG)
UiaTextRange::AddRef()
{
    Tracing::s_TraceUia(this, ApiCall::AddRef, nullptr);
    return InterlockedIncrement(&_cRefs);
}

IFACEMETHODIMP_(ULONG)
UiaTextRange::Release()
{
    Tracing::s_TraceUia(this, ApiCall::Release, nullptr);

    const long val = InterlockedDecrement(&_cRefs);
    if (val == 0)
    {
        delete this;
    }
    return val;
}

IFACEMETHODIMP UiaTextRange::QueryInterface(_In_ REFIID riid, _COM_Outptr_result_maybenull_ void** ppInterface)
{
    Tracing::s_TraceUia(this, ApiCall::QueryInterface, nullptr);

    if (riid == __uuidof(IUnknown))
    {
        *ppInterface = static_cast<ITextRangeProvider*>(this);
    }
    else if (riid == __uuidof(ITextRangeProvider))
    {
        *ppInterface = static_cast<ITextRangeProvider*>(this);
    }
    else
    {
        *ppInterface = nullptr;
        return E_NOINTERFACE;
    }

    (static_cast<IUnknown*>(*ppInterface))->AddRef();
    return S_OK;
}

#pragma endregion

#pragma region ITextRangeProvider

IFACEMETHODIMP UiaTextRange::Clone(_Outptr_result_maybenull_ ITextRangeProvider** ppRetVal)
{
    try
    {
        *ppRetVal = new UiaTextRange(*this);
    }
    catch (...)
    {
        *ppRetVal = nullptr;
        return wil::ResultFromCaughtException();
    }
    if (*ppRetVal == nullptr)
    {
        return E_OUTOFMEMORY;
    }

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"Clone\n");
    std::wstringstream ss;
    ss << _id << L" cloned to " << (static_cast<UiaTextRange*>(*ppRetVal))->_id;
    std::wstring str = ss.str();
    OutputDebugString(str.c_str());
    OutputDebugString(L"\n");
#endif
    // tracing
    ApiMsgClone apiMsg;
    apiMsg.CloneId = static_cast<UiaTextRange*>(*ppRetVal)->GetId();
    Tracing::s_TraceUia(this, ApiCall::Clone, &apiMsg);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::Compare(_In_opt_ ITextRangeProvider* pRange, _Out_ BOOL* pRetVal)
{
    CONSOLE_INFORMATION& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    gci.LockConsole();
    auto Unlock = wil::scope_exit([&] {
        gci.UnlockConsole();
    });

    *pRetVal = FALSE;
    UiaTextRange* other = static_cast<UiaTextRange*>(pRange);
    if (other)
    {
        *pRetVal = !!(_start == other->GetStart() &&
                      _end == other->GetEnd() &&
                      _degenerate == other->IsDegenerate());
    }
    // tracing
    ApiMsgCompare apiMsg;
    apiMsg.OtherId = other == nullptr ? InvalidId : other->GetId();
    apiMsg.Equal = !!*pRetVal;
    Tracing::s_TraceUia(this, ApiCall::Compare, &apiMsg);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::CompareEndpoints(_In_ TextPatternRangeEndpoint endpoint,
                                              _In_ ITextRangeProvider* pTargetRange,
                                              _In_ TextPatternRangeEndpoint targetEndpoint,
                                              _Out_ int* pRetVal)
{
    // get the text range that we're comparing to
    UiaTextRange* range = static_cast<UiaTextRange*>(pTargetRange);
    if (range == nullptr)
    {
        return E_INVALIDARG;
    }

    // get endpoint value that we're comparing to
    Endpoint theirValue;
    if (targetEndpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        theirValue = range->GetStart();
    }
    else
    {
        theirValue = range->GetEnd() + 1;
    }

    // get the values of our endpoint
    Endpoint ourValue;
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        ourValue = _start;
    }
    else
    {
        ourValue = _end + 1;
    }

    // compare them
    *pRetVal = std::clamp(static_cast<int>(ourValue) - static_cast<int>(theirValue), -1, 1);

    // tracing
    ApiMsgCompareEndpoints apiMsg;
    apiMsg.OtherId = range->GetId();
    apiMsg.Endpoint = endpoint;
    apiMsg.TargetEndpoint = targetEndpoint;
    apiMsg.Result = *pRetVal;
    Tracing::s_TraceUia(this, ApiCall::CompareEndpoints, &apiMsg);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::ExpandToEnclosingUnit(_In_ TextUnit unit)
{
    ServiceLocator::LocateGlobals().getConsoleInformation().LockConsole();
    auto Unlock = wil::scope_exit([&] {
        ServiceLocator::LocateGlobals().getConsoleInformation().UnlockConsole();
    });

    ApiMsgExpandToEnclosingUnit apiMsg;
    apiMsg.Unit = unit;
    apiMsg.OriginalStart = _start;
    apiMsg.OriginalEnd = _end;

    try
    {
        const ScreenInfoRow topRow = _getFirstScreenInfoRowIndex();
        const ScreenInfoRow bottomRow = _getLastScreenInfoRowIndex();

        if (unit == TextUnit::TextUnit_Character)
        {
            _end = _start;
        }
        else if (unit <= TextUnit::TextUnit_Line)
        {
            // expand to line
            _start = _textBufferRowToEndpoint(_endpointToTextBufferRow(_start));
            _end = _start + _getLastColumnIndex();
            FAIL_FAST_IF(!(_start <= _end));
        }
        else
        {
            // expand to document
            _start = _screenInfoRowToEndpoint(topRow);
            _end = _screenInfoRowToEndpoint(bottomRow) + _getLastColumnIndex();
        }

        _degenerate = false;

        Tracing::s_TraceUia(this, ApiCall::ExpandToEnclosingUnit, &apiMsg);

        return S_OK;
    }
    CATCH_RETURN();
}

// we don't support this currently
IFACEMETHODIMP UiaTextRange::FindAttribute(_In_ TEXTATTRIBUTEID /*textAttributeId*/,
                                           _In_ VARIANT /*val*/,
                                           _In_ BOOL /*searchBackward*/,
                                           _Outptr_result_maybenull_ ITextRangeProvider** /*ppRetVal*/)
{
    Tracing::s_TraceUia(this, ApiCall::FindAttribute, nullptr);
    return E_NOTIMPL;
}

IFACEMETHODIMP UiaTextRange::FindText(_In_ BSTR text,
                                      _In_ BOOL searchBackward,
                                      _In_ BOOL ignoreCase,
                                      _Outptr_result_maybenull_ ITextRangeProvider** ppRetVal)
{
    Tracing::s_TraceUia(this, ApiCall::FindText, nullptr);

    *ppRetVal = nullptr;
    try
    {
        const std::wstring wstr{ text, SysStringLen(text) };
        const auto sensitivity = ignoreCase ? Search::Sensitivity::CaseInsensitive : Search::Sensitivity::CaseSensitive;

        auto searchDirection = Search::Direction::Forward;
        Endpoint searchAnchor = _start;
        if (searchBackward)
        {
            searchDirection = Search::Direction::Backward;
            searchAnchor = _end;
        }

        Search searcher{ _getScreenInfo(), wstr, searchDirection, sensitivity, _endpointToCoord(searchAnchor) };

        HRESULT hr = S_OK;
        if (searcher.FindNext())
        {
            const auto foundLocation = searcher.GetFoundLocation();
            const Endpoint start = _coordToEndpoint(foundLocation.first);
            const Endpoint end = _coordToEndpoint(foundLocation.second);
            // make sure what was found is within the bounds of the current range
            if ((searchDirection == Search::Direction::Forward && end < _end) ||
                (searchDirection == Search::Direction::Backward && start > _start))
            {
                hr = Clone(ppRetVal);
                if (SUCCEEDED(hr))
                {
                    UiaTextRange& range = static_cast<UiaTextRange&>(**ppRetVal);
                    range._start = start;
                    range._end = end;
                    range._degenerate = false;
                }
            }
        }
        return hr;
    }
    CATCH_RETURN();
}

IFACEMETHODIMP UiaTextRange::GetAttributeValue(_In_ TEXTATTRIBUTEID textAttributeId,
                                               _Out_ VARIANT* pRetVal)
{
    Tracing::s_TraceUia(this, ApiCall::GetAttributeValue, nullptr);
    if (textAttributeId == UIA_IsReadOnlyAttributeId)
    {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_FALSE;
    }
    else
    {
        pRetVal->vt = VT_UNKNOWN;
        UiaGetReservedNotSupportedValue(&pRetVal->punkVal);
    }
    return S_OK;
}

IFACEMETHODIMP UiaTextRange::GetBoundingRectangles(_Outptr_result_maybenull_ SAFEARRAY** ppRetVal)
{
    CONSOLE_INFORMATION& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    gci.LockConsole();
    auto Unlock = wil::scope_exit([&] {
        gci.UnlockConsole();
    });

    *ppRetVal = nullptr;

    try
    {
        // vector to put coords into. they go in as four doubles in the
        // order: left, top, width, height. each line will have its own
        // set of coords.
        std::vector<double> coords;
        const TextBufferRow startRow = _endpointToTextBufferRow(_start);

        if (_degenerate && _isScreenInfoRowInViewport(startRow))
        {
            _addScreenInfoRowBoundaries(_textBufferRowToScreenInfoRow(startRow), coords);
        }
        else
        {
            const unsigned int totalRowsInRange = _rowCountInRange();
            for (unsigned int i = 0; i < totalRowsInRange; ++i)
            {
                ScreenInfoRow screenInfoRow = _textBufferRowToScreenInfoRow(startRow + i);
                if (!_isScreenInfoRowInViewport(screenInfoRow))
                {
                    continue;
                }
                _addScreenInfoRowBoundaries(screenInfoRow, coords);
            }
        }

        // convert to a safearray
        *ppRetVal = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(coords.size()));
        if (*ppRetVal == nullptr)
        {
            return E_OUTOFMEMORY;
        }
        HRESULT hr;
        for (LONG i = 0; i < static_cast<LONG>(coords.size()); ++i)
        {
            hr = SafeArrayPutElement(*ppRetVal, &i, &coords[i]);
            if (FAILED(hr))
            {
                SafeArrayDestroy(*ppRetVal);
                *ppRetVal = nullptr;
                return hr;
            }
        }
    }
    CATCH_RETURN();

    Tracing::s_TraceUia(this, ApiCall::GetBoundingRectangles, nullptr);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::GetEnclosingElement(_Outptr_result_maybenull_ IRawElementProviderSimple** ppRetVal)
{
    Tracing::s_TraceUia(this, ApiCall::GetBoundingRectangles, nullptr);
    return _pProvider->QueryInterface(IID_PPV_ARGS(ppRetVal));
}

IFACEMETHODIMP UiaTextRange::GetText(_In_ int maxLength, _Out_ BSTR* pRetVal)
{
    CONSOLE_INFORMATION& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    gci.LockConsole();
    auto Unlock = wil::scope_exit([&] {
        gci.UnlockConsole();
    });

    std::wstring wstr = L"";

    if (maxLength < -1)
    {
        return E_INVALIDARG;
    }
    // the caller must pass in a value for the max length of the text
    // to retrieve. a value of -1 means they don't want the text
    // truncated.
    const bool getPartialText = maxLength != -1;

    if (!_degenerate)
    {
        try
        {
            const ScreenInfoRow startScreenInfoRow = _endpointToScreenInfoRow(_start);
            const Column startColumn = _endpointToColumn(_start);
            const ScreenInfoRow endScreenInfoRow = _endpointToScreenInfoRow(_end);
            const Column endColumn = _endpointToColumn(_end);
            const unsigned int totalRowsInRange = _rowCountInRange();
            const TextBuffer& textBuffer = _getTextBuffer();

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
            std::wstringstream ss;
            ss << L"---Initial span start=" << _start << L" and end=" << _end << L"\n";
            ss << L"----Retrieving sr:" << startScreenInfoRow << L" sc:" << startColumn << L" er:" << endScreenInfoRow << L" ec:" << endColumn << L"\n";
            OutputDebugString(ss.str().c_str());
#endif

            ScreenInfoRow currentScreenInfoRow;
            for (unsigned int i = 0; i < totalRowsInRange; ++i)
            {
                currentScreenInfoRow = startScreenInfoRow + i;
                const ROW& row = textBuffer.GetRowByOffset(currentScreenInfoRow);
                if (row.GetCharRow().ContainsText())
                {
                    const size_t rowRight = row.GetCharRow().MeasureRight();
                    size_t startIndex = 0;
                    size_t endIndex = rowRight;
                    if (currentScreenInfoRow == startScreenInfoRow)
                    {
                        startIndex = startColumn;
                    }
                    if (currentScreenInfoRow == endScreenInfoRow)
                    {
                        // prevent the end from going past the last non-whitespace char in the row
                        endIndex = std::min(static_cast<size_t>(endColumn + 1), rowRight);
                    }

                    // if startIndex >= endIndex then _start is
                    // further to the right than the last
                    // non-whitespace char in the row so there
                    // wouldn't be any text to grab.
                    if (startIndex < endIndex)
                    {
                        wstr += row.GetText().substr(startIndex, endIndex - startIndex);
                    }
                }

                if (currentScreenInfoRow != endScreenInfoRow)
                {
                    wstr += L"\r\n";
                }

                if (getPartialText && wstr.size() > static_cast<size_t>(maxLength))
                {
                    wstr.resize(maxLength);
                    break;
                }
            }
        }
        CATCH_RETURN();
    }

    *pRetVal = SysAllocString(wstr.c_str());

    // tracing
    ApiMsgGetText apiMsg;
    apiMsg.Text = wstr.c_str();
    Tracing::s_TraceUia(this, ApiCall::GetText, &apiMsg);

#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    std::wstringstream ss;
    ss << L"--------Retrieved Text Max Length(" << maxLength << L") [" << _id << L"]: " << wstr.c_str() << "\n";
    OutputDebugString(ss.str().c_str());
#endif

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::Move(_In_ TextUnit unit,
                                  _In_ int count,
                                  _Out_ int* pRetVal)
{
    ServiceLocator::LocateGlobals().getConsoleInformation().LockConsole();
    auto Unlock = wil::scope_exit([&] {
        ServiceLocator::LocateGlobals().getConsoleInformation().UnlockConsole();
    });

    *pRetVal = 0;
    if (count == 0)
    {
        return S_OK;
    }

    ApiMsgMove apiMsg;
    apiMsg.OriginalStart = _start;
    apiMsg.OriginalEnd = _end;
    apiMsg.Unit = unit;
    apiMsg.RequestedCount = count;
#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"Move\n");
    _outputObjectState();

    std::wstringstream ss;
    ss << L" count: " << count;
    std::wstring data = ss.str();
    OutputDebugString(data.c_str());
    OutputDebugString(L"\n");
    _outputRowConversions();
#endif

    auto moveFunc = &_moveByDocument;
    if (unit == TextUnit::TextUnit_Character)
    {
        moveFunc = &_moveByCharacter;
    }
    else if (unit <= TextUnit::TextUnit_Line)
    {
        moveFunc = &_moveByLine;
    }

    MovementDirection moveDirection = (count > 0) ? MovementDirection::Forward : MovementDirection::Backward;
    std::pair<Endpoint, Endpoint> newEndpoints;

    try
    {
        MoveState moveState{ *this, moveDirection };
        newEndpoints = moveFunc(count,
                                moveState,
                                pRetVal);
    }
    CATCH_RETURN();

    _start = newEndpoints.first;
    _end = newEndpoints.second;

    // a range can't be degenerate after both endpoints have been
    // moved.
    _degenerate = false;

    // tracing
    apiMsg.MovedCount = *pRetVal;
    Tracing::s_TraceUia(this, ApiCall::Move, &apiMsg);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::MoveEndpointByUnit(_In_ TextPatternRangeEndpoint endpoint,
                                                _In_ TextUnit unit,
                                                _In_ int count,
                                                _Out_ int* pRetVal)
{
    ServiceLocator::LocateGlobals().getConsoleInformation().LockConsole();
    auto Unlock = wil::scope_exit([&] {
        ServiceLocator::LocateGlobals().getConsoleInformation().UnlockConsole();
    });

    *pRetVal = 0;
    if (count == 0)
    {
        return S_OK;
    }

    ApiMsgMoveEndpointByUnit apiMsg;
    apiMsg.OriginalStart = _start;
    apiMsg.OriginalEnd = _end;
    apiMsg.Endpoint = endpoint;
    apiMsg.Unit = unit;
    apiMsg.RequestedCount = count;
#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"MoveEndpointByUnit\n");
    _outputObjectState();

    std::wstringstream ss;
    ss << L" endpoint: " << endpoint;
    ss << L" count: " << count;
    std::wstring data = ss.str();
    OutputDebugString(data.c_str());
    OutputDebugString(L"\n");
    _outputRowConversions();
#endif

    MovementDirection moveDirection = (count > 0) ? MovementDirection::Forward : MovementDirection::Backward;

    auto moveFunc = &_moveEndpointByUnitDocument;
    if (unit == TextUnit::TextUnit_Character)
    {
        moveFunc = &_moveEndpointByUnitCharacter;
    }
    else if (unit <= TextUnit::TextUnit_Line)
    {
        moveFunc = &_moveEndpointByUnitLine;
    }

    std::tuple<Endpoint, Endpoint, bool> moveResults;
    try
    {
        MoveState moveState{ *this, moveDirection };
        moveResults = moveFunc(count, endpoint, moveState, pRetVal);
    }
    CATCH_RETURN();

    _start = std::get<0>(moveResults);
    _end = std::get<1>(moveResults);
    _degenerate = std::get<2>(moveResults);

    // tracing
    apiMsg.MovedCount = *pRetVal;
    Tracing::s_TraceUia(this, ApiCall::MoveEndpointByUnit, &apiMsg);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::MoveEndpointByRange(_In_ TextPatternRangeEndpoint endpoint,
                                                 _In_ ITextRangeProvider* pTargetRange,
                                                 _In_ TextPatternRangeEndpoint targetEndpoint)
{
    ServiceLocator::LocateGlobals().getConsoleInformation().LockConsole();
    auto Unlock = wil::scope_exit([&] {
        ServiceLocator::LocateGlobals().getConsoleInformation().UnlockConsole();
    });

    UiaTextRange* range = static_cast<UiaTextRange*>(pTargetRange);
    if (range == nullptr)
    {
        return E_INVALIDARG;
    }

    ApiMsgMoveEndpointByRange apiMsg;
    apiMsg.OriginalStart = _start;
    apiMsg.OriginalEnd = _end;
    apiMsg.Endpoint = endpoint;
    apiMsg.TargetEndpoint = targetEndpoint;
    apiMsg.OtherId = range->GetId();
#if defined(_DEBUG) && defined(UIATEXTRANGE_DEBUG_MSGS)
    OutputDebugString(L"MoveEndpointByRange\n");
    _outputObjectState();

    std::wstringstream ss;
    ss << L" endpoint: " << endpoint;
    ss << L" targetRange: " << range->_id;
    ss << L" targetEndpoint: " << targetEndpoint;
    std::wstring data = ss.str();
    OutputDebugString(data.c_str());
    OutputDebugString(L"\n");
    _outputRowConversions();
#endif

    // get the value that we're updating to
    Endpoint targetEndpointValue;
    if (targetEndpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        targetEndpointValue = range->GetStart();

        // If we're moving our end relative to their start, we actually have to back up one from
        // their start position because this operation treats it as exclusive.
        if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_End)
        {
            if (targetEndpointValue > 0)
            {
                targetEndpointValue--;
            }
        }
    }
    else
    {
        targetEndpointValue = range->GetEnd();

        // If we're moving our start relative to their end, we actually have to sit one after
        // their end position as it was stored inclusive and we're doing this as an exclusive operation.
        if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
        {
            targetEndpointValue++;
        }
    }

    // convert then endpoints to screen info rows/columns
    ScreenInfoRow startScreenInfoRow;
    Column startColumn;
    ScreenInfoRow endScreenInfoRow;
    Column endColumn;
    ScreenInfoRow targetScreenInfoRow;
    Column targetColumn;
    try
    {
        startScreenInfoRow = _endpointToScreenInfoRow(_start);
        startColumn = _endpointToColumn(_start);
        endScreenInfoRow = _endpointToScreenInfoRow(_end);
        endColumn = _endpointToColumn(_end);
        targetScreenInfoRow = _endpointToScreenInfoRow(targetEndpointValue);
        targetColumn = _endpointToColumn(targetEndpointValue);
    }
    CATCH_RETURN();

    // set endpoint value and check for crossed endpoints
    bool crossedEndpoints = false;
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        _start = targetEndpointValue;
        if (_compareScreenCoords(endScreenInfoRow, endColumn, targetScreenInfoRow, targetColumn) == -1)
        {
            // endpoints were crossed
            _end = _start;
            crossedEndpoints = true;
        }
    }
    else
    {
        _end = targetEndpointValue;
        if (_compareScreenCoords(startScreenInfoRow, startColumn, targetScreenInfoRow, targetColumn) == 1)
        {
            // endpoints were crossed
            _start = _end;
            crossedEndpoints = true;
        }
    }
    _degenerate = crossedEndpoints;

    Tracing::s_TraceUia(this, ApiCall::MoveEndpointByRange, &apiMsg);
    return S_OK;
}

IFACEMETHODIMP UiaTextRange::Select()
{
    CONSOLE_INFORMATION& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    gci.LockConsole();
    auto Unlock = wil::scope_exit([&] {
        gci.UnlockConsole();
    });

    if (_degenerate)
    {
        // calling Select on a degenerate range should clear any current selections
        Selection::Instance().ClearSelection();
    }
    else
    {
        COORD coordStart;
        COORD coordEnd;

        coordStart.X = static_cast<SHORT>(_endpointToColumn(_start));
        coordStart.Y = static_cast<SHORT>(_endpointToScreenInfoRow(_start));

        coordEnd.X = static_cast<SHORT>(_endpointToColumn(_end));
        coordEnd.Y = static_cast<SHORT>(_endpointToScreenInfoRow(_end));

        Selection::Instance().SelectNewRegion(coordStart, coordEnd);
    }

    Tracing::s_TraceUia(this, ApiCall::Select, nullptr);
    return S_OK;
}

// we don't support this
IFACEMETHODIMP UiaTextRange::AddToSelection()
{
    Tracing::s_TraceUia(this, ApiCall::AddToSelection, nullptr);
    return E_NOTIMPL;
}

// we don't support this
IFACEMETHODIMP UiaTextRange::RemoveFromSelection()
{
    Tracing::s_TraceUia(this, ApiCall::RemoveFromSelection, nullptr);
    return E_NOTIMPL;
}

IFACEMETHODIMP UiaTextRange::ScrollIntoView(_In_ BOOL alignToTop)
{
    CONSOLE_INFORMATION& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    gci.LockConsole();
    auto Unlock = wil::scope_exit([&] {
        gci.UnlockConsole();
    });

    SMALL_RECT oldViewport;
    unsigned int viewportHeight;
    // range rows
    ScreenInfoRow startScreenInfoRow;
    ScreenInfoRow endScreenInfoRow;
    // screen buffer rows
    ScreenInfoRow topRow;
    ScreenInfoRow bottomRow;
    try
    {
        oldViewport = _getViewport().ToInclusive();
        viewportHeight = _getViewportHeight(oldViewport);
        // range rows
        startScreenInfoRow = _endpointToScreenInfoRow(_start);
        endScreenInfoRow = _endpointToScreenInfoRow(_end);
        // screen buffer rows
        topRow = _getFirstScreenInfoRowIndex();
        bottomRow = _getLastScreenInfoRowIndex();
    }
    CATCH_RETURN();

    SMALL_RECT newViewport = oldViewport;

    // there's a bunch of +1/-1s here for setting the viewport. These
    // are to account for the inclusivity of the viewport boundaries.
    if (alignToTop)
    {
        // determine if we can align the start row to the top
        if (startScreenInfoRow + viewportHeight <= bottomRow)
        {
            // we can align to the top
            newViewport.Top = static_cast<SHORT>(startScreenInfoRow);
            newViewport.Bottom = static_cast<SHORT>(startScreenInfoRow + viewportHeight - 1);
        }
        else
        {
            // we can align to the top so we'll just move the viewport
            // to the bottom of the screen buffer
            newViewport.Bottom = static_cast<SHORT>(bottomRow);
            newViewport.Top = static_cast<SHORT>(bottomRow - viewportHeight + 1);
        }
    }
    else
    {
        // we need to align to the bottom
        // check if we can align to the bottom
        if (endScreenInfoRow >= viewportHeight)
        {
            // we can align to bottom
            newViewport.Bottom = static_cast<SHORT>(endScreenInfoRow);
            newViewport.Top = static_cast<SHORT>(endScreenInfoRow - viewportHeight + 1);
        }
        else
        {
            // we can't align to bottom so we'll move the viewport to
            // the top of the screen buffer
            newViewport.Top = static_cast<SHORT>(topRow);
            newViewport.Bottom = static_cast<SHORT>(topRow + viewportHeight - 1);
        }
    }

    FAIL_FAST_IF(!(newViewport.Top >= static_cast<SHORT>(topRow)));
    FAIL_FAST_IF(!(newViewport.Bottom <= static_cast<SHORT>(bottomRow)));
    FAIL_FAST_IF(!(_getViewportHeight(oldViewport) == _getViewportHeight(newViewport)));

    try
    {
        IConsoleWindow* pIConsoleWindow = _getIConsoleWindow();
        pIConsoleWindow->ChangeViewport(newViewport);
    }
    CATCH_RETURN();

    // tracing
    ApiMsgScrollIntoView apiMsg;
    apiMsg.AlignToTop = !!alignToTop;
    Tracing::s_TraceUia(this, ApiCall::ScrollIntoView, &apiMsg);

    return S_OK;
}

IFACEMETHODIMP UiaTextRange::GetChildren(_Outptr_result_maybenull_ SAFEARRAY** ppRetVal)
{
    Tracing::s_TraceUia(this, ApiCall::GetChildren, nullptr);
    // we don't have any children
    *ppRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    if (*ppRetVal == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

#pragma endregion

// Routine Description:
// - Gets the current viewport
// Arguments:
// - <none>
// Return Value:
// - The screen info's current viewport
const Microsoft::Console::Types::Viewport& UiaTextRange::_getViewport()
{
    return _getScreenInfo().GetViewport();
}

// Routine Description:
// - Gets the current window
// Arguments:
// - <none>
// Return Value:
// - The current window. May return nullptr if there is no current
// window.
Microsoft::Console::Interactivity::IConsoleWindow* const UiaTextRange::_getIConsoleWindow()
{
    using namespace Microsoft::Console::Interactivity;
    IConsoleWindow* const pIConsoleWindow = ServiceLocator::LocateConsoleWindow();
    THROW_HR_IF_NULL(E_POINTER, pIConsoleWindow);
    return pIConsoleWindow;
}

// Routine Description:
// - gets the current window handle
// Arguments:
// - <none>
// Return Value
// - the current window handle
HWND UiaTextRange::_getWindowHandle()
{
    return _getIConsoleWindow()->GetWindowHandle();
}

// Routine Description:
// - gets the current screen info
// Arguments:
// - <none>
// Return Value
// - the current screen info. May return nullptr.
SCREEN_INFORMATION& UiaTextRange::_getScreenInfo()
{
    CONSOLE_INFORMATION& gci = ServiceLocator::LocateGlobals().getConsoleInformation();
    THROW_HR_IF(E_POINTER, !gci.HasActiveOutputBuffer());
    return gci.GetActiveOutputBuffer().GetActiveBuffer();
}

// Routine Description:
// - gets the current output text buffer
// Arguments:
// - <none>
// Return Value
// - the current output text buffer. May return nullptr.
TextBuffer& UiaTextRange::_getTextBuffer()
{
    return _getScreenInfo().GetTextBuffer();
}

// Routine Description:
// - Gets the number of rows in the output text buffer.
// Arguments:
// - <none>
// Return Value:
// - The number of rows
const unsigned int UiaTextRange::_getTotalRows()
{
    return _getTextBuffer().TotalRowCount();
}

// Routine Description:
// - gets the current screen buffer size.
// Arguments:
// - <none>
// Return Value:
// - The screen buffer size
const COORD UiaTextRange::_getScreenBufferCoords()
{
    return _getScreenInfo().GetBufferSize().Dimensions();
}

// Routine Description:
// - Gets the width of the screen buffer rows
// Arguments:
// - <none>
// Return Value:
// - The row width
const unsigned int UiaTextRange::_getRowWidth()
{
    // make sure that we can't leak a 0
    return std::max(static_cast<unsigned int>(_getScreenBufferCoords().X), 1u);
}

// Routine Description:
// - calculates the column refered to by the endpoint.
// Arguments:
// - endpoint - the endpoint to translate
// Return Value:
// - the column value
const Column UiaTextRange::_endpointToColumn(const Endpoint endpoint)
{
    return endpoint % _getRowWidth();
}

// Routine Description:
// - converts an Endpoint into its equivalent text buffer row.
// Arguments:
// - endpoint - the endpoint to convert
// Return Value:
// - the text buffer row value
const TextBufferRow UiaTextRange::_endpointToTextBufferRow(const Endpoint endpoint)
{
    return endpoint / _getRowWidth();
}

// Routine Description:
// - counts the number of rows that are fully or partially part of the
// range.
// Arguments:
// - <none>
// Return Value:
// - The number of rows in the range.
const unsigned int UiaTextRange::_rowCountInRange() const
{
    if (_degenerate)
    {
        return 0;
    }

    const ScreenInfoRow startScreenInfoRow = _endpointToScreenInfoRow(_start);
    const Column startColumn = _endpointToColumn(_start);
    const ScreenInfoRow endScreenInfoRow = _endpointToScreenInfoRow(_end);
    const Column endColumn = _endpointToColumn(_end);

    FAIL_FAST_IF(!(_compareScreenCoords(startScreenInfoRow, startColumn, endScreenInfoRow, endColumn) <= 0));

    // + 1 to balance subtracting ScreenInfoRows from each other
    return endScreenInfoRow - startScreenInfoRow + 1;
}

// Routine Description:
// - Converts a TextBufferRow to a ScreenInfoRow.
// Arguments:
// - row - the TextBufferRow to convert
// Return Value:
// - the equivalent ScreenInfoRow.
const ScreenInfoRow UiaTextRange::_textBufferRowToScreenInfoRow(const TextBufferRow row)
{
    const int firstRowIndex = _getTextBuffer().GetFirstRowIndex();
    return _normalizeRow(row - firstRowIndex);
}

// Routine Description:
// - Converts a ScreenInfoRow to a ViewportRow. Uses the default
// viewport for the conversion.
// Arguments:
// - row - the ScreenInfoRow to convert
// Return Value:
// - the equivalent ViewportRow.
const ViewportRow UiaTextRange::_screenInfoRowToViewportRow(const ScreenInfoRow row)
{
    const SMALL_RECT viewport = _getViewport().ToInclusive();
    return _screenInfoRowToViewportRow(row, viewport);
}

// Routine Description:
// - Converts a ScreenInfoRow to a ViewportRow.
// Arguments:
// - row - the ScreenInfoRow to convert
// - viewport - the viewport to use for the conversion
// Return Value:
// - the equivalent ViewportRow.
const ViewportRow UiaTextRange::_screenInfoRowToViewportRow(const ScreenInfoRow row,
                                                            const SMALL_RECT viewport)
{
    return row - viewport.Top;
}

// Routine Description:
// - normalizes the row index to within the bounds of the output
// buffer. The output buffer stores the text in a circular buffer so
// this method makes sure that we circle around gracefully.
// Arguments:
// - the non-normalized row index
// Return Value:
// - the normalized row index
const Row UiaTextRange::_normalizeRow(const Row row)
{
    const unsigned int totalRows = _getTotalRows();
    return ((row + totalRows) % totalRows);
}

// Routine Description:
// - Gets the viewport height, measured in char rows.
// Arguments:
// - viewport - The viewport to measure
// Return Value:
// - The viewport height
const unsigned int UiaTextRange::_getViewportHeight(const SMALL_RECT viewport)
{
    FAIL_FAST_IF(!(viewport.Bottom >= viewport.Top));
    // + 1 because COORD is inclusive on both sides so subtracting top
    // and bottom gets rid of 1 more then it should.
    return viewport.Bottom - viewport.Top + 1;
}

// Routine Description:
// - Gets the viewport width, measured in char columns.
// Arguments:
// - viewport - The viewport to measure
// Return Value:
// - The viewport width
const unsigned int UiaTextRange::_getViewportWidth(const SMALL_RECT viewport)
{
    FAIL_FAST_IF(!(viewport.Right >= viewport.Left));

    // + 1 because COORD is inclusive on both sides so subtracting left
    // and right gets rid of 1 more then it should.
    return (viewport.Right - viewport.Left + 1);
}

// Routine Description:
// - checks if the row is currently visible in the viewport. Uses the
// default viewport.
// Arguments:
// - row - the screen info row to check
// Return Value:
// - true if the row is within the bounds of the viewport
const bool UiaTextRange::_isScreenInfoRowInViewport(const ScreenInfoRow row)
{
    return _isScreenInfoRowInViewport(row, _getViewport().ToInclusive());
}

// Routine Description:
// - checks if the row is currently visible in the viewport
// Arguments:
// - row - the row to check
// - viewport - the viewport to use for the bounds
// Return Value:
// - true if the row is within the bounds of the viewport
const bool UiaTextRange::_isScreenInfoRowInViewport(const ScreenInfoRow row,
                                                    const SMALL_RECT viewport)
{
    ViewportRow viewportRow = _screenInfoRowToViewportRow(row, viewport);
    return viewportRow >= 0 &&
           viewportRow < static_cast<ViewportRow>(_getViewportHeight(viewport));
}

// Routine Description:
// - Converts a ScreenInfoRow to a TextBufferRow.
// Arguments:
// - row - the ScreenInfoRow to convert
// Return Value:
// - the equivalent TextBufferRow.
const TextBufferRow UiaTextRange::_screenInfoRowToTextBufferRow(const ScreenInfoRow row)
{
    const TextBufferRow firstRowIndex = _getTextBuffer().GetFirstRowIndex();
    return _normalizeRow(row + firstRowIndex);
}

// Routine Description:
// - Converts a TextBufferRow to an Endpoint.
// Arguments:
// - row - the TextBufferRow to convert
// Return Value:
// - the equivalent Endpoint, starting at the beginning of the TextBufferRow.
const Endpoint UiaTextRange::_textBufferRowToEndpoint(const TextBufferRow row)
{
    return _getRowWidth() * row;
}

// Routine Description:
// - Converts a ScreenInfoRow to an Endpoint.
// Arguments:
// - row - the ScreenInfoRow to convert
// Return Value:
// - the equivalent Endpoint.
const Endpoint UiaTextRange::_screenInfoRowToEndpoint(const ScreenInfoRow row)
{
    return _textBufferRowToEndpoint(_screenInfoRowToTextBufferRow(row));
}

// Routine Description:
// - Converts an Endpoint to an ScreenInfoRow.
// Arguments:
// - endpoint - the endpoint to convert
// Return Value:
// - the equivalent ScreenInfoRow.
const ScreenInfoRow UiaTextRange::_endpointToScreenInfoRow(const Endpoint endpoint)
{
    return _textBufferRowToScreenInfoRow(_endpointToTextBufferRow(endpoint));
}

// Routine Description:
// - adds the relevant coordinate points from screenInfoRow to coords.
// Arguments:
// - screenInfoRow - row to calculate coordinate positions from
// - coords - vector to add the calucated coords to
// Return Value:
// - <none>
// Notes:
// - alters coords. may throw an exception.
void UiaTextRange::_addScreenInfoRowBoundaries(const ScreenInfoRow screenInfoRow,
                                               _Inout_ std::vector<double>& coords) const
{
    const SCREEN_INFORMATION& screenInfo = _getScreenInfo();
    const COORD currentFontSize = screenInfo.GetScreenFontSize();

    POINT topLeft;
    POINT bottomRight;

    if (_endpointToScreenInfoRow(_start) == screenInfoRow)
    {
        // start is somewhere in this row so we start from its position
        topLeft.x = _endpointToColumn(_start) * currentFontSize.X;
    }
    else
    {
        // otherwise we start from the beginning of the row
        topLeft.x = 0;
    }

    topLeft.y = _screenInfoRowToViewportRow(screenInfoRow) * currentFontSize.Y;

    if (_endpointToScreenInfoRow(_end) == screenInfoRow)
    {
        // the endpoints are on the same row
        bottomRight.x = (_endpointToColumn(_end) + 1) * currentFontSize.X;
    }
    else
    {
        // _end is not on this row so span to the end of the row
        bottomRight.x = _getViewportWidth(_getViewport().ToInclusive()) * currentFontSize.X;
    }

    // we add the font height only once here because we are adding each line individually
    bottomRight.y = topLeft.y + currentFontSize.Y;

    // convert the coords to be relative to the screen instead of
    // the client window
    HWND hwnd = _getWindowHandle();
    ClientToScreen(hwnd, &topLeft);
    ClientToScreen(hwnd, &bottomRight);

    const LONG width = bottomRight.x - topLeft.x;
    const LONG height = bottomRight.y - topLeft.y;

    // insert the coords
    coords.push_back(topLeft.x);
    coords.push_back(topLeft.y);
    coords.push_back(width);
    coords.push_back(height);
}

// Routine Description:
// - returns the index of the first row of the screen info
// Arguments:
// - <none>
// Return Value:
// - the index of the first row (0-indexed) of the screen info
const unsigned int UiaTextRange::_getFirstScreenInfoRowIndex()
{
    return 0;
}

// Routine Description:
// - returns the index of the last row of the screen info
// Arguments:
// - <none>
// Return Value:
// - the index of the last row (0-indexed) of the screen info
const unsigned int UiaTextRange::_getLastScreenInfoRowIndex()
{
    return _getTotalRows() - 1;
}

// Routine Description:
// - returns the index of the first column of the screen info rows
// Arguments:
// - <none>
// Return Value:
// - the index of the first column (0-indexed) of the screen info rows
const Column UiaTextRange::_getFirstColumnIndex()
{
    return 0;
}

// Routine Description:
// - returns the index of the last column of the screen info rows
// Arguments:
// - <none>
// Return Value:
// - the index of the last column (0-indexed) of the screen info rows
const Column UiaTextRange::_getLastColumnIndex()
{
    return _getRowWidth() - 1;
}

// Routine Description:
// - Compares two sets of screen info coordinates
// Arguments:
// - rowA - the row index of the first position
// - colA - the column index of the first position
// - rowB - the row index of the second position
// - colB - the column index of the second position
// Return Value:
//   -1 if A < B
//    1 if A > B
//    0 if A == B
const int UiaTextRange::_compareScreenCoords(const ScreenInfoRow rowA,
                                             const Column colA,
                                             const ScreenInfoRow rowB,
                                             const Column colB)
{
    FAIL_FAST_IF(!(rowA >= _getFirstScreenInfoRowIndex()));
    FAIL_FAST_IF(!(rowA <= _getLastScreenInfoRowIndex()));

    FAIL_FAST_IF(!(colA >= _getFirstColumnIndex()));
    FAIL_FAST_IF(!(colA <= _getLastColumnIndex()));

    FAIL_FAST_IF(!(rowB >= _getFirstScreenInfoRowIndex()));
    FAIL_FAST_IF(!(rowB <= _getLastScreenInfoRowIndex()));

    FAIL_FAST_IF(!(colB >= _getFirstColumnIndex()));
    FAIL_FAST_IF(!(colB <= _getLastColumnIndex()));

    if (rowA < rowB)
    {
        return -1;
    }
    else if (rowA > rowB)
    {
        return 1;
    }
    // rowA == rowB
    else if (colA < colB)
    {
        return -1;
    }
    else if (colA > colB)
    {
        return 1;
    }
    // colA == colB
    return 0;
}

// Routine Description:
// - calculates new Endpoints if they were to be moved moveCount times
// by character.
// Arguments:
// - moveCount - the number of times to move
// - moveState - values indicating the state of the console for the
// move operation
// - pAmountMoved - the number of times that the return values are "moved"
// Return Value:
// - a pair of endpoints of the form <start, end>
std::pair<Endpoint, Endpoint> UiaTextRange::_moveByCharacter(const int moveCount,
                                                             const MoveState moveState,
                                                             _Out_ int* const pAmountMoved)
{
    if (moveState.Direction == MovementDirection::Forward)
    {
        return _moveByCharacterForward(moveCount, moveState, pAmountMoved);
    }
    else
    {
        return _moveByCharacterBackward(moveCount, moveState, pAmountMoved);
    }
}

std::pair<Endpoint, Endpoint> UiaTextRange::_moveByCharacterForward(const int moveCount,
                                                                    const MoveState moveState,
                                                                    _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;
    int count = moveCount;
    ScreenInfoRow currentScreenInfoRow = moveState.StartScreenInfoRow;
    Column currentColumn = moveState.StartColumn;

    for (int i = 0; i < abs(count); ++i)
    {
        // get the current row's right
        const ROW& row = _getTextBuffer().GetRowByOffset(currentScreenInfoRow);
        const size_t right = row.GetCharRow().MeasureRight();

        // check if we're at the edge of the screen info buffer
        if (currentScreenInfoRow == moveState.LimitingRow &&
            currentColumn + 1 >= right)
        {
            break;
        }
        else if (currentColumn + 1 >= right)
        {
            // we're at the edge of a row and need to go to the next one
            currentColumn = moveState.FirstColumnInRow;
            currentScreenInfoRow += static_cast<int>(moveState.Increment);
        }
        else
        {
            // moving somewhere away from the edges of a row
            currentColumn += static_cast<int>(moveState.Increment);
        }
        *pAmountMoved += static_cast<int>(moveState.Increment);

        FAIL_FAST_IF(!(currentColumn >= _getFirstColumnIndex()));
        FAIL_FAST_IF(!(currentColumn <= _getLastColumnIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));
    }

    Endpoint start = _screenInfoRowToEndpoint(currentScreenInfoRow) + currentColumn;
    Endpoint end = start;
    return std::make_pair<Endpoint, Endpoint>(std::move(start), std::move(end));
}

std::pair<Endpoint, Endpoint> UiaTextRange::_moveByCharacterBackward(const int moveCount,
                                                                     const MoveState moveState,
                                                                     _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;
    int count = moveCount;
    ScreenInfoRow currentScreenInfoRow = moveState.StartScreenInfoRow;
    Column currentColumn = moveState.StartColumn;

    for (int i = 0; i < abs(count); ++i)
    {
        // check if we're at the edge of the screen info buffer
        if (currentScreenInfoRow == moveState.LimitingRow &&
            currentColumn == moveState.LastColumnInRow)
        {
            break;
        }
        else if (currentColumn == moveState.LastColumnInRow)
        {
            // we're at the edge of a row and need to go to the
            // next one. move to the cell with the last non-whitespace charactor

            currentScreenInfoRow += static_cast<int>(moveState.Increment);
            // get the right cell for the next row
            const ROW& row = _getTextBuffer().GetRowByOffset(currentScreenInfoRow);
            const size_t right = row.GetCharRow().MeasureRight();
            currentColumn = static_cast<Column>((right == 0) ? 0 : right - 1);
        }
        else
        {
            // moving somewhere away from the edges of a row
            currentColumn += static_cast<int>(moveState.Increment);
        }
        *pAmountMoved += static_cast<int>(moveState.Increment);

        FAIL_FAST_IF(!(currentColumn >= _getFirstColumnIndex()));
        FAIL_FAST_IF(!(currentColumn <= _getLastColumnIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));
    }

    Endpoint start = _screenInfoRowToEndpoint(currentScreenInfoRow) + currentColumn;
    Endpoint end = start;
    return std::make_pair<Endpoint, Endpoint>(std::move(start), std::move(end));
}

// Routine Description:
// - calculates new Endpoints if they were to be moved moveCount times
// by line.
// Arguments:
// - moveCount - the number of times to move
// - moveState - values indicating the state of the console for the
// move operation
// - pAmountMoved - the number of times that the return values are "moved"
// Return Value:
// - a pair of endpoints of the form <start, end>
std::pair<Endpoint, Endpoint> UiaTextRange::_moveByLine(const int moveCount,
                                                        const MoveState moveState,
                                                        _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;
    Endpoint start = _screenInfoRowToEndpoint(moveState.StartScreenInfoRow) + moveState.StartColumn;
    Endpoint end = _screenInfoRowToEndpoint(moveState.EndScreenInfoRow) + moveState.EndColumn;
    ScreenInfoRow currentScreenInfoRow = moveState.StartScreenInfoRow;
    // we don't want to move the range if we're already in the
    // limiting row and trying to move off the end of the screen buffer
    const bool illegalMovement = (currentScreenInfoRow == moveState.LimitingRow &&
                                  ((moveCount < 0 && moveState.Increment == MovementIncrement::Backward) ||
                                   (moveCount > 0 && moveState.Increment == MovementIncrement::Forward)));

    if (moveCount != 0 && !illegalMovement)
    {
        // move the range
        for (int i = 0; i < abs(moveCount); ++i)
        {
            if (currentScreenInfoRow == moveState.LimitingRow)
            {
                break;
            }
            currentScreenInfoRow += static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);

            FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
            FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));
        }
        start = _screenInfoRowToEndpoint(currentScreenInfoRow);
        end = start + _getLastColumnIndex();
    }

    return std::make_pair<Endpoint, Endpoint>(std::move(start), std::move(end));
}

// Routine Description:
// - calculates new Endpoints if they were to be moved moveCount times
// by document.
// Arguments:
// - moveCount - the number of times to move
// - moveState - values indicating the state of the console for the
// move operation
// - pAmountMoved - the number of times that the return values are "moved"
// Return Value:
// - a pair of endpoints of the form <start, end>
std::pair<Endpoint, Endpoint> UiaTextRange::_moveByDocument(const int /*moveCount*/,
                                                            const MoveState moveState,
                                                            _Out_ int* const pAmountMoved)
{
    // We can't move by anything larger than a line, so move by document will apply and will
    // just report that it can't do that.
    *pAmountMoved = 0;

    // We then have to return the same endpoints as what we initially had so nothing happens.
    Endpoint start = _screenInfoRowToEndpoint(moveState.StartScreenInfoRow) + moveState.StartColumn;
    Endpoint end = _screenInfoRowToEndpoint(moveState.EndScreenInfoRow) + moveState.EndColumn;

    return std::make_pair<Endpoint, Endpoint>(std::move(start), std::move(end));
}

// Routine Description:
// - calculates new Endpoints/degenerate state if the indicated
// endpoint was moved moveCount times by character.
// Arguments:
// - moveCount - the number of times to move
// - endpoint - the endpoint to move
// - moveState - values indicating the state of the console for the
// move operation
// - pAmountMoved - the number of times that the return values are "moved"
// Return Value:
// - A tuple of elements of the form <start, end, degenerate>
std::tuple<Endpoint, Endpoint, bool> UiaTextRange::_moveEndpointByUnitCharacter(const int moveCount,
                                                                                const TextPatternRangeEndpoint endpoint,
                                                                                const MoveState moveState,
                                                                                _Out_ int* const pAmountMoved)
{
    if (moveState.Direction == MovementDirection::Forward)
    {
        return _moveEndpointByUnitCharacterForward(moveCount, endpoint, moveState, pAmountMoved);
    }
    else
    {
        return _moveEndpointByUnitCharacterBackward(moveCount, endpoint, moveState, pAmountMoved);
    }
}

std::tuple<Endpoint, Endpoint, bool>
UiaTextRange::_moveEndpointByUnitCharacterForward(const int moveCount,
                                                  const TextPatternRangeEndpoint endpoint,
                                                  const MoveState moveState,
                                                  _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;
    int count = moveCount;
    ScreenInfoRow currentScreenInfoRow;
    Column currentColumn;

    // set current location vars
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        currentScreenInfoRow = moveState.StartScreenInfoRow;
        currentColumn = moveState.StartColumn;
    }
    else
    {
        currentScreenInfoRow = moveState.EndScreenInfoRow;
        currentColumn = moveState.EndColumn;
    }

    for (int i = 0; i < abs(count); ++i)
    {
        // get the current row's right
        const ROW& row = _getTextBuffer().GetRowByOffset(currentScreenInfoRow);
        const size_t right = row.GetCharRow().MeasureRight();

        // check if we're at the edge of the screen info buffer
        if (currentScreenInfoRow == moveState.LimitingRow &&
            currentColumn + 1 >= right)
        {
            break;
        }
        else if (currentColumn + 1 >= right)
        {
            // we're at the edge of a row and need to go to the next one
            currentColumn = moveState.FirstColumnInRow;
            currentScreenInfoRow += static_cast<int>(moveState.Increment);
        }
        else
        {
            // moving somewhere away from the edges of a row
            currentColumn += static_cast<int>(moveState.Increment);
        }
        *pAmountMoved += static_cast<int>(moveState.Increment);

        FAIL_FAST_IF(!(currentColumn >= _getFirstColumnIndex()));
        FAIL_FAST_IF(!(currentColumn <= _getLastColumnIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));
    }

    // translate the row back to an endpoint and handle any crossed endpoints
    Endpoint convertedEndpoint = _screenInfoRowToEndpoint(currentScreenInfoRow) + currentColumn;
    Endpoint start = _screenInfoRowToEndpoint(moveState.StartScreenInfoRow) + moveState.StartColumn;
    Endpoint end = _screenInfoRowToEndpoint(moveState.EndScreenInfoRow) + moveState.EndColumn;
    bool degenerate = false;
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        start = convertedEndpoint;
        if (_compareScreenCoords(currentScreenInfoRow,
                                 currentColumn,
                                 moveState.EndScreenInfoRow,
                                 moveState.EndColumn) == 1)
        {
            end = start;
            degenerate = true;
        }
    }
    else
    {
        end = convertedEndpoint;
        if (_compareScreenCoords(currentScreenInfoRow,
                                 currentColumn,
                                 moveState.StartScreenInfoRow,
                                 moveState.StartColumn) == -1)
        {
            start = end;
            degenerate = true;
        }
    }
    return std::make_tuple(start, end, degenerate);
}

std::tuple<Endpoint, Endpoint, bool>
UiaTextRange::_moveEndpointByUnitCharacterBackward(const int moveCount,
                                                   const TextPatternRangeEndpoint endpoint,
                                                   const MoveState moveState,
                                                   _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;
    int count = moveCount;
    ScreenInfoRow currentScreenInfoRow;
    Column currentColumn;

    // set current location vars
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        currentScreenInfoRow = moveState.StartScreenInfoRow;
        currentColumn = moveState.StartColumn;
    }
    else
    {
        currentScreenInfoRow = moveState.EndScreenInfoRow;
        currentColumn = moveState.EndColumn;
    }

    for (int i = 0; i < abs(count); ++i)
    {
        // check if we're at the edge of the screen info buffer
        if (currentScreenInfoRow == moveState.LimitingRow &&
            currentColumn == moveState.LastColumnInRow)
        {
            break;
        }
        else if (currentColumn == moveState.LastColumnInRow)
        {
            // we're at the edge of a row and need to go to the
            // next one. move to the cell with the last non-whitespace charactor

            currentScreenInfoRow += static_cast<int>(moveState.Increment);
            // get the right cell for the next row
            const ROW& row = _getTextBuffer().GetRowByOffset(currentScreenInfoRow);
            const size_t right = row.GetCharRow().MeasureRight();
            currentColumn = static_cast<Column>((right == 0) ? 0 : right - 1);
        }
        else
        {
            // moving somewhere away from the edges of a row
            currentColumn += static_cast<int>(moveState.Increment);
        }
        *pAmountMoved += static_cast<int>(moveState.Increment);

        FAIL_FAST_IF(!(currentColumn >= _getFirstColumnIndex()));
        FAIL_FAST_IF(!(currentColumn <= _getLastColumnIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));
    }

    // translate the row back to an endpoint and handle any crossed endpoints
    Endpoint convertedEndpoint = _screenInfoRowToEndpoint(currentScreenInfoRow) + currentColumn;
    Endpoint start = _screenInfoRowToEndpoint(moveState.StartScreenInfoRow) + moveState.StartColumn;
    Endpoint end = _screenInfoRowToEndpoint(moveState.EndScreenInfoRow) + moveState.EndColumn;
    bool degenerate = false;
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        start = convertedEndpoint;
        if (_compareScreenCoords(currentScreenInfoRow,
                                 currentColumn,
                                 moveState.EndScreenInfoRow,
                                 moveState.EndColumn) == 1)
        {
            end = start;
            degenerate = true;
        }
    }
    else
    {
        end = convertedEndpoint;
        if (_compareScreenCoords(currentScreenInfoRow,
                                 currentColumn,
                                 moveState.StartScreenInfoRow,
                                 moveState.StartColumn) == -1)
        {
            start = end;
            degenerate = true;
        }
    }
    return std::make_tuple(start, end, degenerate);
}

// Routine Description:
// - calculates new Endpoints/degenerate state if the indicated
// endpoint was moved moveCount times by line.
// Arguments:
// - moveCount - the number of times to move
// - endpoint - the endpoint to move
// - moveState - values indicating the state of the console for the
// move operation
// - pAmountMoved - the number of times that the return values are "moved"
// Return Value:
// - A tuple of elements of the form <start, end, degenerate>
std::tuple<Endpoint, Endpoint, bool> UiaTextRange::_moveEndpointByUnitLine(const int moveCount,
                                                                           const TextPatternRangeEndpoint endpoint,
                                                                           const MoveState moveState,
                                                                           _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;
    int count = moveCount;
    ScreenInfoRow currentScreenInfoRow;
    Column currentColumn;
    bool forceDegenerate = false;
    Endpoint start = _screenInfoRowToEndpoint(moveState.StartScreenInfoRow) + moveState.StartColumn;
    Endpoint end = _screenInfoRowToEndpoint(moveState.EndScreenInfoRow) + moveState.EndColumn;
    bool degenerate = false;

    if (moveCount == 0)
    {
        return std::make_tuple(start, end, degenerate);
    }

    MovementDirection moveDirection = (moveCount > 0) ? MovementDirection::Forward : MovementDirection::Backward;

    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        currentScreenInfoRow = moveState.StartScreenInfoRow;
        currentColumn = moveState.StartColumn;
    }
    else
    {
        currentScreenInfoRow = moveState.EndScreenInfoRow;
        currentColumn = moveState.EndColumn;
    }

    // check if we can't be moved anymore
    if (currentScreenInfoRow == moveState.LimitingRow &&
        currentColumn == moveState.LastColumnInRow)
    {
        return std::make_tuple(start, end, degenerate);
    }
    else if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start &&
             moveDirection == MovementDirection::Forward)
    {
        if (moveState.StartScreenInfoRow == moveState.LimitingRow)
        {
            // _start is somewhere on the limiting row but not at
            // the very end. move to the end of the last row
            count -= static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);
            currentColumn = _getLastColumnIndex();
            forceDegenerate = true;
        }
        if (moveState.StartColumn != _getFirstColumnIndex())
        {
            // _start is somewhere in the middle of a row, so do a
            // partial movement to the beginning of the next row
            count -= static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);
            currentScreenInfoRow += static_cast<int>(moveState.Increment);
            currentColumn = _getFirstColumnIndex();
        }
    }
    else if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start &&
             moveDirection == MovementDirection::Backward)
    {
        if (moveState.StartColumn != _getFirstColumnIndex())
        {
            // moving backwards when we weren't already at the beginning of
            // the row so move there first to align with the text unit boundary
            count -= static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);
            currentColumn = _getFirstColumnIndex();
        }
    }
    else if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_End &&
             moveDirection == MovementDirection::Forward)
    {
        if (moveState.EndColumn != _getLastColumnIndex())
        {
            // _end is not at the last column in a row, so we move
            // forward to it with a partial movement
            count -= static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);
            currentColumn = _getLastColumnIndex();
        }
    }
    else
    {
        // _end moving backwards
        if (moveState.EndScreenInfoRow == moveState.LimitingRow)
        {
            // _end is somewhere on the limiting row but not at the
            // front. move it there
            count -= static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);
            currentColumn = _getFirstColumnIndex();
            forceDegenerate = true;
        }
        else if (moveState.EndColumn != _getLastColumnIndex())
        {
            // _end is not at the last column in a row, so we move it
            // backwards to it with a partial move
            count -= static_cast<int>(moveState.Increment);
            *pAmountMoved += static_cast<int>(moveState.Increment);
            currentColumn = _getLastColumnIndex();
            currentScreenInfoRow += static_cast<int>(moveState.Increment);
        }
    }

    FAIL_FAST_IF(!(currentColumn >= _getFirstColumnIndex()));
    FAIL_FAST_IF(!(currentColumn <= _getLastColumnIndex()));
    FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
    FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));

    // move the row that the endpoint corresponds to
    while (count != 0 && currentScreenInfoRow != moveState.LimitingRow)
    {
        count -= static_cast<int>(moveState.Increment);
        currentScreenInfoRow += static_cast<int>(moveState.Increment);
        *pAmountMoved += static_cast<int>(moveState.Increment);

        FAIL_FAST_IF(!(currentScreenInfoRow >= _getFirstScreenInfoRowIndex()));
        FAIL_FAST_IF(!(currentScreenInfoRow <= _getLastScreenInfoRowIndex()));
    }

    // translate the row back to an endpoint and handle any crossed endpoints
    Endpoint convertedEndpoint = _screenInfoRowToEndpoint(currentScreenInfoRow) + currentColumn;
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        start = convertedEndpoint;
        if (currentScreenInfoRow > moveState.EndScreenInfoRow || forceDegenerate)
        {
            degenerate = true;
            end = start;
        }
    }
    else
    {
        end = convertedEndpoint;
        if (currentScreenInfoRow < moveState.StartScreenInfoRow || forceDegenerate)
        {
            degenerate = true;
            start = end;
        }
    }

    return std::make_tuple(start, end, degenerate);
}

// Routine Description:
// - calculates new Endpoints/degenerate state if the indicate
// endpoint was moved moveCount times by document.
// Arguments:
// - moveCount - the number of times to move
// - endpoint - the endpoint to move
// - moveState - values indicating the state of the console for the
// move operation
// - pAmountMoved - the number of times that the return values are "moved"
// Return Value:
// - A tuple of elements of the form <start, end, degenerate>
std::tuple<Endpoint, Endpoint, bool> UiaTextRange::_moveEndpointByUnitDocument(const int moveCount,
                                                                               const TextPatternRangeEndpoint endpoint,
                                                                               const MoveState moveState,
                                                                               _Out_ int* const pAmountMoved)
{
    *pAmountMoved = 0;

    Endpoint start;
    Endpoint end;
    bool degenerate = false;
    if (endpoint == TextPatternRangeEndpoint::TextPatternRangeEndpoint_Start)
    {
        if (moveCount < 0)
        {
            // moving _start backwards
            start = _screenInfoRowToEndpoint(_getFirstScreenInfoRowIndex()) + _getFirstColumnIndex();
            end = _screenInfoRowToEndpoint(moveState.EndScreenInfoRow) + moveState.EndColumn;
            if (!(moveState.StartScreenInfoRow == _getFirstScreenInfoRowIndex() &&
                  moveState.StartColumn == _getFirstColumnIndex()))
            {
                *pAmountMoved += static_cast<int>(moveState.Increment);
            }
        }
        else
        {
            // moving _start forwards
            start = _screenInfoRowToEndpoint(_getLastScreenInfoRowIndex()) + _getLastColumnIndex();
            end = start;
            degenerate = true;
            if (!(moveState.StartScreenInfoRow == _getLastScreenInfoRowIndex() &&
                  moveState.StartColumn == _getLastColumnIndex()))
            {
                *pAmountMoved += static_cast<int>(moveState.Increment);
            }
        }
    }
    else
    {
        if (moveCount < 0)
        {
            // moving _end backwards
            end = _screenInfoRowToEndpoint(_getFirstScreenInfoRowIndex()) + _getFirstColumnIndex();
            start = end;
            degenerate = true;
            if (!(moveState.EndScreenInfoRow == _getFirstScreenInfoRowIndex() &&
                  moveState.EndColumn == _getFirstColumnIndex()))
            {
                *pAmountMoved += static_cast<int>(moveState.Increment);
            }
        }
        else
        {
            // moving _end forwards
            end = _screenInfoRowToEndpoint(_getLastScreenInfoRowIndex()) + _getLastColumnIndex();
            start = _screenInfoRowToEndpoint(moveState.StartScreenInfoRow) + moveState.StartColumn;
            if (!(moveState.EndScreenInfoRow == _getLastScreenInfoRowIndex() &&
                  moveState.EndColumn == _getLastColumnIndex()))
            {
                *pAmountMoved += static_cast<int>(moveState.Increment);
            }
        }
    }

    return std::make_tuple(start, end, degenerate);
}

COORD UiaTextRange::_endpointToCoord(const Endpoint endpoint)
{
    return { gsl::narrow<short>(_endpointToColumn(endpoint)), gsl::narrow<short>(_endpointToScreenInfoRow(endpoint)) };
}

Endpoint UiaTextRange::_coordToEndpoint(const COORD coord)
{
    return _screenInfoRowToEndpoint(coord.Y) + coord.X;
}
