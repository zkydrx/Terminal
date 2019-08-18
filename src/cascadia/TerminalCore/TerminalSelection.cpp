// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Terminal.hpp"

using namespace Microsoft::Terminal::Core;

// Method Description:
// - Helper to determine the selected region of the buffer. Used for rendering.
// Return Value:
// - A vector of rectangles representing the regions to select, line by line. They are absolute coordinates relative to the buffer origin.
std::vector<SMALL_RECT> Terminal::_GetSelectionRects() const
{
    std::vector<SMALL_RECT> selectionArea;

    if (!_selectionActive)
    {
        return selectionArea;
    }

    // create these new anchors for comparison and rendering
    COORD selectionAnchorWithOffset{ _selectionAnchor };
    COORD endSelectionPositionWithOffset{ _endSelectionPosition };

    // Add anchor offset here to update properly on new buffer output
    THROW_IF_FAILED(ShortAdd(selectionAnchorWithOffset.Y, _selectionAnchor_YOffset, &selectionAnchorWithOffset.Y));
    THROW_IF_FAILED(ShortAdd(endSelectionPositionWithOffset.Y, _endSelectionPosition_YOffset, &endSelectionPositionWithOffset.Y));

    // clamp Y values to be within mutable viewport bounds
    selectionAnchorWithOffset.Y = std::clamp(selectionAnchorWithOffset.Y, static_cast<SHORT>(0), _mutableViewport.BottomInclusive());
    endSelectionPositionWithOffset.Y = std::clamp(endSelectionPositionWithOffset.Y, static_cast<SHORT>(0), _mutableViewport.BottomInclusive());

    // clamp X values to be within buffer bounds
    const auto bufferSize = _buffer->GetSize();
    selectionAnchorWithOffset.X = std::clamp(_selectionAnchor.X, bufferSize.Left(), bufferSize.RightInclusive());
    endSelectionPositionWithOffset.X = std::clamp(_endSelectionPosition.X, bufferSize.Left(), bufferSize.RightInclusive());

    // NOTE: (0,0) is top-left so vertical comparison is inverted
    const COORD& higherCoord = (selectionAnchorWithOffset.Y <= endSelectionPositionWithOffset.Y) ?
                                   selectionAnchorWithOffset :
                                   endSelectionPositionWithOffset;
    const COORD& lowerCoord = (selectionAnchorWithOffset.Y > endSelectionPositionWithOffset.Y) ?
                                  selectionAnchorWithOffset :
                                  endSelectionPositionWithOffset;

    selectionArea.reserve(lowerCoord.Y - higherCoord.Y + 1);
    for (auto row = higherCoord.Y; row <= lowerCoord.Y; row++)
    {
        SMALL_RECT selectionRow;

        selectionRow.Top = row;
        selectionRow.Bottom = row;

        if (_boxSelection || higherCoord.Y == lowerCoord.Y)
        {
            selectionRow.Left = std::min(higherCoord.X, lowerCoord.X);
            selectionRow.Right = std::max(higherCoord.X, lowerCoord.X);
        }
        else
        {
            selectionRow.Left = (row == higherCoord.Y) ? higherCoord.X : 0;
            selectionRow.Right = (row == lowerCoord.Y) ? lowerCoord.X : bufferSize.RightInclusive();
        }

        // expand selection for Double/Triple Click
        if (_multiClickSelectionMode == SelectionExpansionMode::Word)
        {
            const auto cellChar = _buffer->GetCellDataAt(selectionAnchorWithOffset)->Chars();
            if (_selectionAnchor == _endSelectionPosition && _isWordDelimiter(cellChar))
            {
                // only highlight the cell if you double click a delimiter
            }
            else
            {
                selectionRow.Left = _ExpandDoubleClickSelectionLeft({ selectionRow.Left, row }).X;
                selectionRow.Right = _ExpandDoubleClickSelectionRight({ selectionRow.Right, row }).X;
            }
        }
        else if (_multiClickSelectionMode == SelectionExpansionMode::Line)
        {
            selectionRow.Left = 0;
            selectionRow.Right = bufferSize.RightInclusive();
        }

        // expand selection for Wide Glyphs
        selectionRow.Left = _ExpandWideGlyphSelectionLeft(selectionRow.Left, row);
        selectionRow.Right = _ExpandWideGlyphSelectionRight(selectionRow.Right, row);

        selectionArea.emplace_back(selectionRow);
    }
    return selectionArea;
}

// Method Description:
// - Expands the selection left-wards to cover a wide glyph, if necessary
// Arguments:
// - position: the (x,y) coordinate on the visible viewport
// Return Value:
// - updated x position to encapsulate the wide glyph
const SHORT Terminal::_ExpandWideGlyphSelectionLeft(const SHORT xPos, const SHORT yPos) const
{
    // don't change the value if at/outside the boundary
    if (xPos <= 0 || xPos > _buffer->GetSize().RightInclusive())
    {
        return xPos;
    }

    COORD position{ xPos, yPos };
    const auto attr = _buffer->GetCellDataAt(position)->DbcsAttr();
    if (attr.IsTrailing())
    {
        // move off by highlighting the lead half too.
        // alters position.X
        _buffer->GetSize().DecrementInBounds(position);
    }
    return position.X;
}

// Method Description:
// - Expands the selection right-wards to cover a wide glyph, if necessary
// Arguments:
// - position: the (x,y) coordinate on the visible viewport
// Return Value:
// - updated x position to encapsulate the wide glyph
const SHORT Terminal::_ExpandWideGlyphSelectionRight(const SHORT xPos, const SHORT yPos) const
{
    // don't change the value if at/outside the boundary
    if (xPos < 0 || xPos >= _buffer->GetSize().RightInclusive())
    {
        return xPos;
    }

    COORD position{ xPos, yPos };
    const auto attr = _buffer->GetCellDataAt(position)->DbcsAttr();
    if (attr.IsLeading())
    {
        // move off by highlighting the trailing half too.
        // alters position.X
        _buffer->GetSize().IncrementInBounds(position);
    }
    return position.X;
}

// Method Description:
// - Checks if selection is active
// Return Value:
// - bool representing if selection is active. Used to decide copy/paste on right click
const bool Terminal::IsSelectionActive() const noexcept
{
    return _selectionActive;
}

// Method Description:
// - Select the sequence between delimiters defined in Settings
// Arguments:
// - position: the (x,y) coordinate on the visible viewport
void Terminal::DoubleClickSelection(const COORD position)
{
    // if you double click a delimiter, just select that one cell
    COORD positionWithOffsets = _ConvertToBufferCell(position);
    const auto cellChar = _buffer->GetCellDataAt(positionWithOffsets)->Chars();
    if (_isWordDelimiter(cellChar))
    {
        SetSelectionAnchor(position);
        _multiClickSelectionMode = SelectionExpansionMode::Word;
        return;
    }

    // scan leftwards until delimiter is found and
    // set selection anchor to one right of that spot
    _selectionAnchor = _ExpandDoubleClickSelectionLeft(positionWithOffsets);
    THROW_IF_FAILED(ShortSub(_selectionAnchor.Y, gsl::narrow<SHORT>(_ViewStartIndex()), &_selectionAnchor.Y));
    _selectionAnchor_YOffset = gsl::narrow<SHORT>(_ViewStartIndex());

    // scan rightwards until delimiter is found and
    // set endSelectionPosition to one left of that spot
    _endSelectionPosition = _ExpandDoubleClickSelectionRight(positionWithOffsets);
    THROW_IF_FAILED(ShortSub(_endSelectionPosition.Y, gsl::narrow<SHORT>(_ViewStartIndex()), &_endSelectionPosition.Y));
    _endSelectionPosition_YOffset = gsl::narrow<SHORT>(_ViewStartIndex());

    _selectionActive = true;
    _multiClickSelectionMode = SelectionExpansionMode::Word;
}

// Method Description:
// - Select the entire row of the position clicked
// Arguments:
// - position: the (x,y) coordinate on the visible viewport
void Terminal::TripleClickSelection(const COORD position)
{
    SetSelectionAnchor({ 0, position.Y });
    SetEndSelectionPosition({ _buffer->GetSize().RightInclusive(), position.Y });

    _multiClickSelectionMode = SelectionExpansionMode::Line;
}

// Method Description:
// - Record the position of the beginning of a selection
// Arguments:
// - position: the (x,y) coordinate on the visible viewport
void Terminal::SetSelectionAnchor(const COORD position)
{
    _selectionAnchor = position;

    // include _scrollOffset here to ensure this maps to the right spot of the original viewport
    THROW_IF_FAILED(ShortSub(_selectionAnchor.Y, gsl::narrow<SHORT>(_scrollOffset), &_selectionAnchor.Y));

    // copy value of ViewStartIndex to support scrolling
    // and update on new buffer output (used in _GetSelectionRects())
    _selectionAnchor_YOffset = gsl::narrow<SHORT>(_ViewStartIndex());

    _selectionActive = true;
    SetEndSelectionPosition(position);

    _multiClickSelectionMode = SelectionExpansionMode::Cell;
}

// Method Description:
// - Record the position of the end of a selection
// Arguments:
// - position: the (x,y) coordinate on the visible viewport
void Terminal::SetEndSelectionPosition(const COORD position)
{
    _endSelectionPosition = position;

    // include _scrollOffset here to ensure this maps to the right spot of the original viewport
    THROW_IF_FAILED(ShortSub(_endSelectionPosition.Y, gsl::narrow<SHORT>(_scrollOffset), &_endSelectionPosition.Y));

    // copy value of ViewStartIndex to support scrolling
    // and update on new buffer output (used in _GetSelectionRects())
    _endSelectionPosition_YOffset = gsl::narrow<SHORT>(_ViewStartIndex());
}

// Method Description:
// - enable/disable box selection (ALT + selection)
// Arguments:
// - isEnabled: new value for _boxSelection
void Terminal::SetBoxSelection(const bool isEnabled) noexcept
{
    _boxSelection = isEnabled;
}

// Method Description:
// - clear selection data and disable rendering it
void Terminal::ClearSelection()
{
    _selectionActive = false;
    _selectionAnchor = { 0, 0 };
    _endSelectionPosition = { 0, 0 };
    _selectionAnchor_YOffset = 0;
    _endSelectionPosition_YOffset = 0;

    _buffer->GetRenderTarget().TriggerSelection();
}

// Method Description:
// - get wstring text from highlighted portion of text buffer
// Arguments:
// - trimTrailingWhitespace: enable removing any whitespace from copied selection
//    and get text to appear on separate lines.
// Return Value:
// - wstring text from buffer. If extended to multiple lines, each line is separated by \r\n
const std::wstring Terminal::RetrieveSelectedTextFromBuffer(bool trimTrailingWhitespace) const
{
    std::function<COLORREF(TextAttribute&)> GetForegroundColor = std::bind(&Terminal::GetForegroundColor, this, std::placeholders::_1);
    std::function<COLORREF(TextAttribute&)> GetBackgroundColor = std::bind(&Terminal::GetBackgroundColor, this, std::placeholders::_1);

    auto data = _buffer->GetTextForClipboard(!_boxSelection,
                                             trimTrailingWhitespace,
                                             _GetSelectionRects(),
                                             GetForegroundColor,
                                             GetBackgroundColor);

    std::wstring result;
    for (const auto& text : data.text)
    {
        result += text;
    }

    return result;
}

// Method Description:
// - expand the double click selection to the left (stopped by delimiter)
// Arguments:
// - position: viewport coordinate for selection
// Return Value:
// - updated copy of "position" to new expanded location (with vertical offset)
COORD Terminal::_ExpandDoubleClickSelectionLeft(const COORD position) const
{
    COORD positionWithOffsets = position;
    const auto bufferViewport = _buffer->GetSize();
    auto cellChar = _buffer->GetCellDataAt(positionWithOffsets)->Chars();
    while (positionWithOffsets.X != 0 && !_isWordDelimiter(cellChar))
    {
        bufferViewport.DecrementInBounds(positionWithOffsets);
        cellChar = _buffer->GetCellDataAt(positionWithOffsets)->Chars();
    }

    if (positionWithOffsets.X != 0 && _isWordDelimiter(cellChar))
    {
        // move off of delimiter to highlight properly
        bufferViewport.IncrementInBounds(positionWithOffsets);
    }

    return positionWithOffsets;
}

// Method Description:
// - expand the double click selection to the right (stopped by delimiter)
// Arguments:
// - position: viewport coordinate for selection
// Return Value:
// - updated copy of "position" to new expanded location (with vertical offset)
COORD Terminal::_ExpandDoubleClickSelectionRight(const COORD position) const
{
    COORD positionWithOffsets = position;
    const auto bufferViewport = _buffer->GetSize();
    auto cellChar = _buffer->GetCellDataAt(positionWithOffsets)->Chars();
    while (positionWithOffsets.X != _buffer->GetSize().RightInclusive() && !_isWordDelimiter(cellChar))
    {
        bufferViewport.IncrementInBounds(positionWithOffsets);
        cellChar = _buffer->GetCellDataAt(positionWithOffsets)->Chars();
    }

    if (positionWithOffsets.X != bufferViewport.RightInclusive() && _isWordDelimiter(cellChar))
    {
        // move off of delimiter to highlight properly
        bufferViewport.DecrementInBounds(positionWithOffsets);
    }

    return positionWithOffsets;
}

// Method Description:
// - check if buffer cell data contains delimiter for double click selection
// Arguments:
// - cellChar: the char saved to the buffer cell under observation
// Return Value:
// - true if cell data contains the delimiter.
const bool Terminal::_isWordDelimiter(std::wstring_view cellChar) const
{
    return _wordDelimiters.find(cellChar) != std::wstring_view::npos;
}

// Method Description:
// - convert viewport position to the corresponding location on the buffer
// Arguments:
// - viewportPos: a coordinate on the viewport
// Return Value:
// - the corresponding location on the buffer
const COORD Terminal::_ConvertToBufferCell(const COORD viewportPos) const
{
    // Force position to be valid
    COORD positionWithOffsets = viewportPos;
    positionWithOffsets.X = std::clamp(viewportPos.X, static_cast<SHORT>(0), _buffer->GetSize().RightInclusive());
    positionWithOffsets.Y = std::clamp(viewportPos.Y, static_cast<SHORT>(0), _buffer->GetSize().BottomInclusive());

    THROW_IF_FAILED(ShortSub(viewportPos.Y, gsl::narrow<SHORT>(_scrollOffset), &positionWithOffsets.Y));
    THROW_IF_FAILED(ShortAdd(positionWithOffsets.Y, gsl::narrow<SHORT>(_ViewStartIndex()), &positionWithOffsets.Y));
    return positionWithOffsets;
}
