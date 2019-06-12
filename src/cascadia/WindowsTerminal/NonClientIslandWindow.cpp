/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/
#include "pch.h"
#include "NonClientIslandWindow.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Composition;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Hosting;
using namespace winrt::Windows::Foundation::Numerics;
using namespace ::Microsoft::Console::Types;

constexpr int RECT_WIDTH(const RECT* const pRect)
{
    return pRect->right - pRect->left;
}
constexpr int RECT_HEIGHT(const RECT* const pRect)
{
    return pRect->bottom - pRect->top;
}

NonClientIslandWindow::NonClientIslandWindow() noexcept :
    IslandWindow{},
    _nonClientInteropWindowHandle{ nullptr },
    _nonClientRootGrid{ nullptr },
    _nonClientSource{ nullptr },
    _maximizedMargins{ 0 },
    _isMaximized{ false }
{
}

NonClientIslandWindow::~NonClientIslandWindow()
{
}

// Method Description:
// - Used to initialize the XAML island for the non-client area. Also calls our
//   base IslandWindow's Initialize, which will initialize the client XAML
//   Island.
void NonClientIslandWindow::Initialize()
{
    _nonClientSource = DesktopWindowXamlSource{};
    auto interop = _nonClientSource.as<IDesktopWindowXamlSourceNative>();
    winrt::check_hresult(interop->AttachToWindow(_window));

    // stash the child interop handle so we can resize it when the main hwnd is resized
    interop->get_WindowHandle(&_nonClientInteropWindowHandle);

    _nonClientRootGrid = winrt::Windows::UI::Xaml::Controls::Grid{};

    _nonClientSource.Content(_nonClientRootGrid);

    // Call the IslandWindow Initialize to set up the client xaml island
    IslandWindow::Initialize();
}

// Method Description:
// - Sets the content of the non-client area of our window to the given XAML element.
// Arguments:
// - content: a XAML element to use as the content of the titlebar.
// Return Value:
// - <none>
void NonClientIslandWindow::SetNonClientContent(winrt::Windows::UI::Xaml::UIElement content)
{
    _nonClientRootGrid.Children().Clear();
    _nonClientRootGrid.Children().Append(content);
}

// Method Description:
// - Set the height we expect to reserve for the non-client content.
// Arguments:
// - contentHeight: the size in pixels we should use for the non-client content.
void NonClientIslandWindow::SetNonClientHeight(const int contentHeight) noexcept
{
    _titlebarUnscaledContentHeight = contentHeight;
}

// Method Description:
// - Gets the size of the content area of the titlebar (the non-client area).
//   This can be padded either by the margins from maximization (when the window
//   is maximized) or the normal window borders.
// Return Value:
// - A Viewport representing the area of the window which should be the titlebar
//   content, in window coordinates.
Viewport NonClientIslandWindow::GetTitlebarContentArea() const noexcept
{
    const auto scale = GetCurrentDpiScale();

    const auto titlebarContentHeight = _titlebarUnscaledContentHeight * scale;
    const auto titlebarMarginRight = _titlebarMarginRight;

    const auto physicalSize = GetPhysicalSize();
    const auto clientWidth = physicalSize.cx;

    auto titlebarWidth = clientWidth - (_windowMarginSides + titlebarMarginRight);
    // Adjust for maximized margins
    titlebarWidth -= (_maximizedMargins.cxLeftWidth + _maximizedMargins.cxRightWidth);

    const auto titlebarHeight = titlebarContentHeight - (_titlebarMarginTop + _titlebarMarginBottom);

    COORD titlebarOrigin = { static_cast<short>(_windowMarginSides),
                             static_cast<short>(_titlebarMarginTop) };

    if (_isMaximized)
    {
        titlebarOrigin.X = static_cast<short>(_maximizedMargins.cxLeftWidth);
        titlebarOrigin.Y = static_cast<short>(_maximizedMargins.cyTopHeight);
    }

    return Viewport::FromDimensions(titlebarOrigin,
                                    { static_cast<short>(titlebarWidth), static_cast<short>(titlebarHeight) });
}

// Method Description:
// - Gets the size of the client content area of the window.
//   This can be padded either by the margins from maximization (when the window
//   is maximized) or the normal window borders.
// Arguments:
// - <none>
// Return Value:
// - A Viewport representing the area of the window which should be the client
//   content, in window coordinates.
Viewport NonClientIslandWindow::GetClientContentArea() const noexcept
{
    MARGINS margins = GetFrameMargins();

    COORD clientOrigin = { static_cast<short>(margins.cxLeftWidth),
                           static_cast<short>(margins.cyTopHeight) };

    const auto physicalSize = GetPhysicalSize();
    auto clientWidth = physicalSize.cx;
    auto clientHeight = physicalSize.cy;

    // If we're maximized, we don't want to use the frame as our margins,
    // instead we want to use the margins from the maximization. If we included
    // the left&right sides of the frame in this calculation while maximized,
    // you' have a few pixels of the window border on the sides while maximized,
    // which most apps do not have.
    if (_isMaximized)
    {
        clientWidth -= (_maximizedMargins.cxLeftWidth + _maximizedMargins.cxRightWidth);
        clientHeight -= (margins.cyTopHeight + _maximizedMargins.cyBottomHeight);
        clientOrigin.X = static_cast<short>(_maximizedMargins.cxLeftWidth);
    }
    else
    {
        // Remove the left and right width of the frame from the client area
        clientWidth -= (margins.cxLeftWidth + margins.cxRightWidth);
        clientHeight -= (margins.cyTopHeight + margins.cyBottomHeight);
    }

    // The top maximization margin is already included in the GetFrameMargins
    // calcualtion.

    return Viewport::FromDimensions(clientOrigin,
                                    { static_cast<short>(clientWidth), static_cast<short>(clientHeight) });
}

// Method Description:
// - called when the size of the window changes for any reason. Updates the
//   sizes of our child Xaml Islands to match our new sizing.
void NonClientIslandWindow::OnSize()
{
    auto clientArea = GetClientContentArea();
    auto titlebarArea = GetTitlebarContentArea();

    // update the interop window size
    SetWindowPos(_interopWindowHandle,
                 0,
                 clientArea.Left(),
                 clientArea.Top(),
                 clientArea.Width(),
                 clientArea.Height(),
                 SWP_SHOWWINDOW);

    if (_rootGrid)
    {
        const SIZE physicalSize{ clientArea.Width(), clientArea.Height() };
        const auto logicalSize = GetLogicalSize(physicalSize);
        _rootGrid.Width(logicalSize.Width);
        _rootGrid.Height(logicalSize.Height);
    }

    // update the interop window size
    SetWindowPos(_nonClientInteropWindowHandle,
                 0,
                 titlebarArea.Left(),
                 titlebarArea.Top(),
                 titlebarArea.Width(),
                 titlebarArea.Height(),
                 SWP_SHOWWINDOW);
}

// Method Description:
// Hit test the frame for resizing and moving.
// Method Description:
// - Hit test the frame for resizing and moving.
// Arguments:
// - ptMouse: the mouse point being tested, in absolute (NOT WINDOW) coordinates.
// Return Value:
// - one of the values from
//  https://docs.microsoft.com/en-us/windows/desktop/inputdev/wm-nchittest#return-value
//   corresponding to the area of the window that was hit
// NOTE:
// Largely taken from code on:
// https://docs.microsoft.com/en-us/windows/desktop/dwm/customframe
[[nodiscard]] LRESULT NonClientIslandWindow::HitTestNCA(POINT ptMouse) const noexcept
{
    // Get the window rectangle.
    RECT rcWindow = BaseWindow::GetWindowRect();

    MARGINS margins = GetFrameMargins();

    // Get the frame rectangle, adjusted for the style without a caption.
    RECT rcFrame = { 0 };
    auto expectedStyle = WS_OVERLAPPEDWINDOW;
    WI_ClearAllFlags(expectedStyle, WS_CAPTION);
    AdjustWindowRectEx(&rcFrame, expectedStyle, false, 0);

    // Determine if the hit test is for resizing. Default middle (1,1).
    unsigned short uRow = 1;
    unsigned short uCol = 1;
    bool fOnResizeBorder = false;

    // Determine if the point is at the top or bottom of the window.
    if (ptMouse.y >= rcWindow.top && ptMouse.y < rcWindow.top + margins.cyTopHeight)
    {
        fOnResizeBorder = (ptMouse.y < (rcWindow.top - rcFrame.top));
        uRow = 0;
    }
    else if (ptMouse.y < rcWindow.bottom && ptMouse.y >= rcWindow.bottom - margins.cyBottomHeight)
    {
        uRow = 2;
    }

    // Determine if the point is at the left or right of the window.
    if (ptMouse.x >= rcWindow.left && ptMouse.x < rcWindow.left + margins.cxLeftWidth)
    {
        uCol = 0; // left side
    }
    else if (ptMouse.x < rcWindow.right && ptMouse.x >= rcWindow.right - margins.cxRightWidth)
    {
        uCol = 2; // right side
    }

    // clang-format off
    // Hit test (HTTOPLEFT, ... HTBOTTOMRIGHT)
    LRESULT hitTests[3][3] = {
        { HTTOPLEFT,    fOnResizeBorder ? HTTOP : HTCAPTION, HTTOPRIGHT },
        { HTLEFT,       HTNOWHERE,                           HTRIGHT },
        { HTBOTTOMLEFT, HTBOTTOM,                            HTBOTTOMRIGHT },
    };
    // clang-format on

    return hitTests[uRow][uCol];
}

// Method Description:
// - Get the size of the borders we want to use. The sides and bottom will just
//   be big enough for resizing, but the top will be as big as we need for the
//   non-client content.
// Return Value:
// - A MARGINS struct containing the border dimensions we want.
MARGINS NonClientIslandWindow::GetFrameMargins() const noexcept
{
    const auto titlebarView = GetTitlebarContentArea();

    MARGINS margins{ 0 };
    margins.cxLeftWidth = _windowMarginSides;
    margins.cxRightWidth = _windowMarginSides;
    margins.cyBottomHeight = _windowMarginBottom;
    margins.cyTopHeight = titlebarView.BottomExclusive();

    return margins;
}

// Method Description:
// - Updates the borders of our window frame, using DwmExtendFrameIntoClientArea.
// Arguments:
// - <none>
// Return Value:
// - the HRESULT returned by DwmExtendFrameIntoClientArea.
[[nodiscard]] HRESULT NonClientIslandWindow::_UpdateFrameMargins() const noexcept
{
    // Get the size of the borders we want to use. The sides and bottom will
    // just be big enough for resizing, but the top will be as big as we need
    // for the non-client content.
    MARGINS margins = GetFrameMargins();
    // Extend the frame into the client area.
    return DwmExtendFrameIntoClientArea(_window, &margins);
}

// Routine Description:
// - Gets the maximum possible window rectangle in pixels. Based on the monitor
//   the window is on or the primary monitor if no window exists yet.
// Arguments:
// - prcSuggested - If we were given a suggested rectangle for where the window
//                  is going, we can pass it in here to find out the max size
//                  on that monitor.
//                - If this value is zero and we had a valid window handle,
//                  we'll use that instead. Otherwise the value of 0 will make
//                  us use the primary monitor.
// - pDpiSuggested - The dpi that matches the suggested rect. We will attempt to
//                   compute this during the function, but if we fail for some
//                   reason, the original value passed in will be left untouched.
// Return Value:
// - RECT containing the left, right, top, and bottom positions from the desktop
//   origin in pixels. Measures the outer edges of the potential window.
// NOTE:
// Heavily taken from WindowMetrics::GetMaxWindowRectInPixels in conhost.
RECT NonClientIslandWindow::GetMaxWindowRectInPixels(const RECT* const prcSuggested,
                                                     _Out_opt_ UINT* pDpiSuggested)
{
    // prepare rectangle
    RECT rc = *prcSuggested;

    // use zero rect to compare.
    RECT rcZero;
    SetRectEmpty(&rcZero);

    // First get the monitor pointer from either the active window or the default location (0,0,0,0)
    HMONITOR hMonitor = nullptr;

    // NOTE: We must use the nearest monitor because sometimes the system moves the window around into strange spots while performing snap and Win+D operations.
    // Those operations won't work correctly if we use MONITOR_DEFAULTTOPRIMARY.
    if (!EqualRect(&rc, &rcZero))
    {
        // For invalid window handles or when we were passed a non-zero suggestion rectangle, get the monitor from the rect.
        hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    }
    else
    {
        // Otherwise, get the monitor from the window handle.
        hMonitor = MonitorFromWindow(_window, MONITOR_DEFAULTTONEAREST);
    }

    // If for whatever reason there is no monitor, we're going to give back whatever we got since we can't figure anything out.
    // We won't adjust the DPI either. That's OK. DPI doesn't make much sense with no display.
    if (nullptr == hMonitor)
    {
        return rc;
    }

    // Now obtain the monitor pixel dimensions
    MONITORINFO MonitorInfo = { 0 };
    MonitorInfo.cbSize = sizeof(MONITORINFO);

    GetMonitorInfoW(hMonitor, &MonitorInfo);

    // We have to make a correction to the work area. If we actually consume the entire work area (by maximizing the window)
    // The window manager will render the borders off-screen.
    // We need to pad the work rectangle with the border dimensions to represent the actual max outer edges of the window rect.
    WINDOWINFO wi = { 0 };
    wi.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(_window, &wi);

    // In non-full screen, we want to only use the work area (avoiding the task bar space)
    rc = MonitorInfo.rcWork;

    if (pDpiSuggested != nullptr)
    {
        UINT monitorDpiX;
        UINT monitorDpiY;
        if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &monitorDpiX, &monitorDpiY)))
        {
            *pDpiSuggested = monitorDpiX;
        }
        else
        {
            *pDpiSuggested = GetDpiForWindow(_window);
        }
    }

    return rc;
}

// Method Description:
// - Handle window messages from the message loop.
// Arguments:
// - message: A window message ID identifying the message.
// - wParam: The contents of this parameter depend on the value of the message parameter.
// - lParam: The contents of this parameter depend on the value of the message parameter.
// Return Value:
// - The return value is the result of the message processing and depends on the
//   message sent.
[[nodiscard]] LRESULT NonClientIslandWindow::MessageHandler(UINT const message,
                                                            WPARAM const wParam,
                                                            LPARAM const lParam) noexcept
{
    LRESULT lRet = 0;

    // First call DwmDefWindowProc. This might handle things like the
    // min/max/close buttons for us.
    const bool dwmHandledMessage = DwmDefWindowProc(_window, message, wParam, lParam, &lRet);

    switch (message)
    {
    case WM_ACTIVATE:
    {
        _HandleActivateWindow();
        break;
    }
    case WM_NCCALCSIZE:
    {
        if (wParam == false)
        {
            return 0;
        }
        // Handle the non-client size message.
        if (wParam == TRUE && lParam)
        {
            // Calculate new NCCALCSIZE_PARAMS based on custom NCA inset.
            NCCALCSIZE_PARAMS* pncsp = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

            pncsp->rgrc[0].left = pncsp->rgrc[0].left + 0;
            pncsp->rgrc[0].top = pncsp->rgrc[0].top + 0;
            pncsp->rgrc[0].right = pncsp->rgrc[0].right - 0;
            pncsp->rgrc[0].bottom = pncsp->rgrc[0].bottom - 0;

            return 0;
        }
        break;
    }
    case WM_NCHITTEST:
    {
        if (dwmHandledMessage)
        {
            return lRet;
        }

        // Handle hit testing in the NCA if not handled by DwmDefWindowProc.
        if (lRet == 0)
        {
            lRet = HitTestNCA({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });

            if (lRet != HTNOWHERE)
            {
                return lRet;
            }
        }
        break;
    }
    case WM_WINDOWPOSCHANGING:
    {
        // Enforce maximum size here instead of WM_GETMINMAXINFO. If we return
        // it in WM_GETMINMAXINFO, then it will be enforced when snapping across
        // DPI boundaries (bad.)
        LPWINDOWPOS lpwpos = reinterpret_cast<LPWINDOWPOS>(lParam);
        if (lpwpos == nullptr)
        {
            break;
        }
        if (_HandleWindowPosChanging(lpwpos))
        {
            return 0;
        }
        else
        {
            break;
        }
    }
    }

    return IslandWindow::MessageHandler(message, wParam, lParam);
}

// Method Description:
// - Handle a WM_ACTIVATE message. Called during the creation of the window, and
//   used as an opprotunity to get the dimensions of the caption buttons (the
//   min, max, close buttons). We'll use these dimensions to help size the
//   non-client area of the window.
void NonClientIslandWindow::_HandleActivateWindow()
{
    const auto dpi = GetDpiForWindow(_window);

    // Use DwmGetWindowAttribute to get the complete size of the caption buttons.
    RECT captionSize{ 0 };
    THROW_IF_FAILED(DwmGetWindowAttribute(_window, DWMWA_CAPTION_BUTTON_BOUNDS, &captionSize, sizeof(RECT)));

    // Divide by 3 to get the width of a single button
    // Multiply by 4 to reserve the space of one button as the "grab handle"
    _titlebarMarginRight = MulDiv(RECT_WIDTH(&captionSize), 4, 3);

    // _titlebarUnscaledContentHeight is set with SetNonClientHeight by the app
    // hosting us.

    THROW_IF_FAILED(_UpdateFrameMargins());
}

// Method Description:
// - Handle a WM_WINDOWPOSCHANGING message. When the window is changing, or the
//   dpi is changing, this handler is triggered to give us a chance to adjust
//   the window size and position manually. We use this handler during a maxiize
//   to figure out by how much the window will overhang the edges of the
//   monitor, and set up some padding to adjust for that.
// Arguments:
// - windowPos: A pointer to a proposed window location and size. Should we wish
//   to manually position the window, we could change the values of this struct.
// Return Value:
// - true if we handled this message, false otherwise. If we return false, the
//   message should instead be handled by DefWindowProc
// Note:
// Largely taken from the conhost WM_WINDOWPOSCHANGING handler.
bool NonClientIslandWindow::_HandleWindowPosChanging(WINDOWPOS* const windowPos)
{
    // We only need to apply restrictions if the size is changing.
    if (WI_IsFlagSet(windowPos->flags, SWP_NOSIZE))
    {
        return false;
    }

    // Figure out the suggested dimensions
    RECT rcSuggested;
    rcSuggested.left = windowPos->x;
    rcSuggested.top = windowPos->y;
    rcSuggested.right = rcSuggested.left + windowPos->cx;
    rcSuggested.bottom = rcSuggested.top + windowPos->cy;
    SIZE szSuggested;
    szSuggested.cx = RECT_WIDTH(&rcSuggested);
    szSuggested.cy = RECT_HEIGHT(&rcSuggested);

    // Figure out the current dimensions for comparison.
    RECT rcCurrent = GetWindowRect();

    // Determine whether we're being resized by someone dragging the edge or
    // completely moved around.
    bool fIsEdgeResize = false;
    {
        // We can only be edge resizing if our existing rectangle wasn't empty.
        // If it was empty, we're doing the initial create.
        if (!IsRectEmpty(&rcCurrent))
        {
            // If one or two sides are changing, we're being edge resized.
            unsigned int cSidesChanging = 0;
            if (rcCurrent.left != rcSuggested.left)
            {
                cSidesChanging++;
            }
            if (rcCurrent.right != rcSuggested.right)
            {
                cSidesChanging++;
            }
            if (rcCurrent.top != rcSuggested.top)
            {
                cSidesChanging++;
            }
            if (rcCurrent.bottom != rcSuggested.bottom)
            {
                cSidesChanging++;
            }

            if (cSidesChanging == 1 || cSidesChanging == 2)
            {
                fIsEdgeResize = true;
            }
        }
    }

    const auto windowStyle = GetWindowStyle(_window);
    const auto isMaximized = WI_IsFlagSet(windowStyle, WS_MAXIMIZE);

    // If we're about to maximize the window, determine how much we're about to
    // overhang by, and adjust for that.
    // We need to do this because maximized windows will typically overhang the
    // actual monitor bounds by roughly the size of the old "thick: window
    // borders. For normal windows, this is fine, but because we're using
    // DwmExtendFrameIntoClientArea, that means some of our client content will
    // now overhang, and get cut off.
    if (isMaximized)
    {
        // Find the related monitor, the maximum pixel size,
        // and the dpi for the suggested rect.
        UINT dpiOfMaximum;
        RECT rcMaximum;

        if (fIsEdgeResize)
        {
            // If someone's dragging from the edge to resize in one direction,
            // we want to make sure we never grow past the current monitor.
            rcMaximum = GetMaxWindowRectInPixels(&rcCurrent, &dpiOfMaximum);
        }
        else
        {
            // In other circumstances, assume we're snapping around or some
            // other jump (TS). Just do whatever we're told using the new
            // suggestion as the restriction monitor.
            rcMaximum = GetMaxWindowRectInPixels(&rcSuggested, &dpiOfMaximum);
        }

        const auto suggestedWidth = szSuggested.cx;
        const auto suggestedHeight = szSuggested.cy;

        const auto maxWidth = RECT_WIDTH(&rcMaximum);
        const auto maxHeight = RECT_HEIGHT(&rcMaximum);

        // Only apply the maximum size restriction if the current DPI matches
        // the DPI of the maximum rect. This keeps us from applying the wrong
        // restriction if the monitor we're moving to has a different DPI but
        // we've yet to get notified of that DPI change. If we do apply it, then
        // we'll restrict the console window BEFORE its been resized for the DPI
        // change, so we're likely to shrink the window too much or worse yet,
        // keep it from moving entirely. We'll get a WM_DPICHANGED, resize the
        // window, and then process the restriction in a few window messages.
        if (((int)dpiOfMaximum == _currentDpi) &&
            ((suggestedWidth > maxWidth) ||
             (suggestedHeight > maxHeight)))
        {
            auto offset = 0;
            // Determine which side of the window to use for the offset
            //  calculation. If the taskbar is on the left or top of the screen,
            //  then the x or y coordinate of the work rect might not be 0.
            //  Check both, and use whichever is 0.
            if (rcMaximum.left == 0)
            {
                offset = windowPos->x;
            }
            else if (rcMaximum.top == 0)
            {
                offset = windowPos->y;
            }
            const auto offsetX = offset;
            const auto offsetY = offset;

            _maximizedMargins.cxRightWidth = -offset;
            _maximizedMargins.cxLeftWidth = -offset;

            _maximizedMargins.cyTopHeight = -offset;
            _maximizedMargins.cyBottomHeight = -offset;

            _isMaximized = true;
            THROW_IF_FAILED(_UpdateFrameMargins());
        }
    }
    else
    {
        // Clear our maximization state
        _maximizedMargins = { 0 };

        // Immediately after resoring down, don't update our frame margins. If
        // you do this here, then a small gap will appear between the titlebar
        // and the content, until the window is moved. However, we do need to
        // keep this here _in general_ for dragging across DPI boundaries.
        if (!_isMaximized)
        {
            THROW_IF_FAILED(_UpdateFrameMargins());
        }

        _isMaximized = false;
    }
    return true;
}
