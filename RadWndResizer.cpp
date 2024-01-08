#define NOMINMAX
#include "Window.h"
#include "Windowxx.h"
#include <tchar.h>
#include "HandlePtr.h"
#include <algorithm>

// TODO
// Needs a tray icon
// Settings for hotkey
// Move window immediatley while menu is still showing

#define HK_MENU 1

#define APPNAME TEXT("RadWndResizer")

inline LONG Width(const RECT& r)
{
    return r.right - r.left;
}

inline LONG Height(const RECT& r)
{
    return r.bottom - r.top;
}

inline RECT Multiply(RECT r, double scale)
{
    r.top = lround(r.top * scale);
    r.left = lround(r.left * scale);
    r.bottom = lround(r.bottom * scale);
    r.right = lround(r.right * scale);
    return r;
}

inline SIZE Size(const RECT& r)
{
    return { Width(r), Height(r) };
}


class RootWindow : public Window
{
    friend WindowManager<RootWindow>;
public:
    static ATOM Register() { return WindowManager<RootWindow>::Register(); }
    static RootWindow* Create() { return WindowManager<RootWindow>::Create(); }

protected:
    static void GetCreateWindow(CREATESTRUCT& cs);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    BOOL OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnDestroy();
    void OnHotKey(int idHotKey, UINT fuModifiers, UINT vk);
    void OnInitMenuPopup(HMENU hMenu, UINT item, BOOL fSystemMenu);

    static LPCTSTR ClassName() { return TEXT("RADWNDRESIZER"); }
    POINT m_InitMenuSelection = {};
};

void RootWindow::GetCreateWindow(CREATESTRUCT& cs)
{
    Window::GetCreateWindow(cs);
    cs.lpszName = APPNAME;
    cs.style = WS_OVERLAPPEDWINDOW;
}

BOOL RootWindow::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (!RegisterHotKey(*this, HK_MENU, MOD_CONTROL | MOD_SHIFT, 'M'))
        MessageBox(NULL, TEXT("Error registering hotkey"), APPNAME, MB_ICONERROR | MB_OK);
    return TRUE;
}

void RootWindow::OnDestroy()
{
    PostQuitMessage(0);
}

HBITMAPPtr CreateGridBitmap(const HDC hDC, const SIZE sz, const SIZE rowcols, const POINT selectedAnchor, const POINT selectedPivot)
{
    HBITMAPPtr hBitmap(CreateCompatibleBitmap(hDC, sz.cx, sz.cy));
    {
        const HDCPtr hMemDC(CreateCompatibleDC(hDC));
        const auto hOldBitmap = SelectTempObject(hMemDC, hBitmap);
        const auto hOldPen = SelectTempObject(hMemDC, GetStockObject(DC_PEN));
        const auto hOldBrush = SelectTempObject(hMemDC, GetStockObject(DC_BRUSH));

        SetDCPenColor(hMemDC, RGB(0, 0, 0));
        SetDCBrushColor(hMemDC, RGB(255, 255, 255));

        Rectangle(hMemDC, 0, 0, sz.cx, sz.cy);
        for (int i = 1; i < rowcols.cx; ++i)
        {
            const int x = (sz.cx * i) / rowcols.cx;
            MoveToEx(hMemDC, x, 0, nullptr);
            LineTo(hMemDC, x, sz.cy);
        }
        for (int j = 1; j < rowcols.cy; ++j)
        {
            const int y = (sz.cy * j) / rowcols.cy;
            MoveToEx(hMemDC, 0, y, nullptr);
            LineTo(hMemDC, sz.cx, y);
        }

        const POINT selectedMin = { std::min(selectedAnchor.x, selectedPivot.x ), std::min(selectedAnchor.y, selectedPivot.y) };
        const POINT selectedMax = { std::max(selectedAnchor.x, selectedPivot.x), std::max(selectedAnchor.y, selectedPivot.y) };

        POINT selected;
        for (selected.y = selectedMin.y; selected.y <= selectedMax.y; ++selected.y)
        {
            for (selected.x = selectedMin.x; selected.x <= selectedMax.x; ++selected.x)
            {
                if (selected.x >= 0 && selected.x < rowcols.cx && selected.y >= 0 && selected.y < rowcols.cy)
                {
                    SetDCBrushColor(hMemDC, RGB(0, 0, 255));
                    Rectangle(hMemDC, (sz.cx * selected.x) / rowcols.cx, (sz.cy * selected.y) / rowcols.cy, (sz.cx * (selected.x + 1)) / rowcols.cx, (sz.cy * (selected.y + 1)) / rowcols.cy);
                }
            }
        }
    }
    return hBitmap;
}

HBITMAPPtr CreateGridBitmap(const HDC hDC, const SIZE sz, const SIZE rowcols, const POINT selected)
{
    return CreateGridBitmap(hDC, sz, rowcols, selected, selected);
}

HBITMAPPtr CreateGridBitmap(const HDC hDC, const SIZE sz, const SIZE rowcols)
{
    return CreateGridBitmap(hDC, sz, rowcols, POINT{ -1, -1 }, POINT{ -1, -1 });
}

void RootWindow::OnHotKey(int idHotKey, UINT fuModifiers, UINT vk)
{
    switch (idHotKey)
    {
    case HK_MENU:
    {
        const LONG fixborder = 8; // Windows 10 invisible border

        const SIZE layouts[] = {
            { 2, 1 },
            { 2, 2 },
            { 2, 3 },
            { 3, 1 },
            { 3, 2 },
            { 3, 3 },
        };

        //HWND hWnd = *this;
        const HWND hWnd = GetForegroundWindow();
        RECT wr;
        GetWindowRect(hWnd, &wr);

        const POINT menupt = { wr.right - fixborder, wr.top + GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYCAPTION) };

        SetForegroundWindow(*this);

        const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

        MONITORINFO moninfo = { sizeof(MONITORINFO) };
        GetMonitorInfo(hMonitor, &moninfo);
        const RECT r = Multiply(moninfo.rcWork, 40.0 / Height(moninfo.rcWork));

        const UINT FIRST_CMD = 2;
        int selected = -1;

        {
            HBITMAPPtr hBitmapSet[ARRAYSIZE(layouts)];
            {
                auto hDC = MakeGetDC(*this);
                for (UINT i = 0; i < ARRAYSIZE(layouts); ++i)
                    hBitmapSet[i] = CreateGridBitmap(hDC.get(), Size(r), layouts[i]);
            }

            HMENUPtr hMenu(CreatePopupMenu());
            UINT i = 0;
            for (const HBITMAPPtr& hBitmap : hBitmapSet)
            {
                //const bool dobreak = ARRAYSIZE(hBitmapSet) > 3 && (i == (ARRAYSIZE(hBitmapSet) / 2));
                const bool dobreak = i > 0 && layouts[i - 1].cx != layouts[i].cx;
                AppendMenu(hMenu, MF_BITMAP | (dobreak ? MF_MENUBREAK : 0), FIRST_CMD + i, reinterpret_cast<LPCTSTR>(hBitmap.get()));
                ++i;
            }

            selected = TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, menupt.x, menupt.y, 0, *this, nullptr) - FIRST_CMD;
        }

        if (selected >= 0 && selected < ARRAYSIZE(layouts))
        {
            const SIZE layout = layouts[selected];
            const UINT count = layout.cx * layout.cy;

            HBITMAPPtr hBitmapSet[100] = {};
            {
                auto hDC = MakeGetDC(*this);
                UINT i = 0;
                POINT selectedAnchor;
                for (selectedAnchor.x = 0; selectedAnchor.x < layout.cx; ++selectedAnchor.x)
                    for (selectedAnchor.y = 0; selectedAnchor.y < layout.cy; ++selectedAnchor.y)
                        hBitmapSet[i++] = CreateGridBitmap(hDC.get(), Size(r), layout, selectedAnchor);
            }

            HMENUPtr hMenu(CreatePopupMenu());
            for (UINT i = 0; i < count; ++i)
            {
                const bool dobreak = (i % (count / layout.cx)) == 0;
                AppendMenu(hMenu, MF_BITMAP | (dobreak ? MF_MENUBREAK : 0), FIRST_CMD + i, reinterpret_cast<LPCTSTR>(hBitmapSet[i].get()));
            }

            selected = TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, menupt.x, menupt.y, 0, *this, nullptr) - FIRST_CMD;

            if (selected >= 0 && selected < static_cast<int>(count))
            {
                const POINT selectedAnchor = { selected / layout.cy, selected % layout.cy };

                {
                    {
                        auto hDC = MakeGetDC(*this);
                        UINT i = 0;
                        POINT selectedPivot;
                        for (selectedPivot.x = 0; selectedPivot.x < layout.cx; ++selectedPivot.x)
                            for (selectedPivot.y = 0; selectedPivot.y < layout.cy; ++selectedPivot.y)
                                hBitmapSet[i++] = CreateGridBitmap(hDC.get(), Size(r), layout, selectedAnchor, selectedPivot);
                    }

                    HMENUPtr hMenu(CreatePopupMenu());
                    for (UINT i = 0; i < count; ++i)
                    {
                        const bool dobreak = (i % (count / layout.cx)) == 0;
                        AppendMenu(hMenu, MF_BITMAP | (dobreak ? MF_MENUBREAK : 0), FIRST_CMD + i, reinterpret_cast<LPCTSTR>(hBitmapSet[i].get()));
                    }

                    m_InitMenuSelection = selectedAnchor;
                    selected = TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, menupt.x, menupt.y, 0, *this, nullptr) - FIRST_CMD;
                }

                if (selected >= 0 && selected < static_cast<int>(count))
                {
                    const POINT selectedPivot = { selected / layout.cy, selected % layout.cy };

                    const POINT selectedMin = { std::min(selectedAnchor.x, selectedPivot.x), std::min(selectedAnchor.y, selectedPivot.y) };
                    const POINT selectedMax = { std::max(selectedAnchor.x, selectedPivot.x) + 1, std::max(selectedAnchor.y, selectedPivot.y) + 1 };

                    RECT newr;
                    newr.left = moninfo.rcWork.left + selectedMin.x * Width(moninfo.rcWork) / layout.cx;
                    newr.right = moninfo.rcWork.left + selectedMax.x * Width(moninfo.rcWork) / layout.cx;
                    newr.top = moninfo.rcWork.top + selectedMin.y * Height(moninfo.rcWork) / layout.cy;
                    newr.bottom = moninfo.rcWork.top + selectedMax.y * Height(moninfo.rcWork) / layout.cy;
                    InflateRect(&newr, fixborder, 0);
                    newr.bottom += fixborder;
                    ShowWindow(hWnd, SW_RESTORE);
                    MoveWindow(hWnd, newr.left, newr.top, Width(newr), Height(newr), TRUE);
                }
            }
        }

        SetForegroundWindow(hWnd);
    }
        break;
    }
}

void RootWindow::OnInitMenuPopup(HMENU hMenu, UINT item, BOOL fSystemMenu)
{
    if (!fSystemMenu)
    {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki = { VK_DOWN, 0, 0 };
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki = { VK_DOWN, 0, KEYEVENTF_KEYUP };
        SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));

        for (LONG i = 0; i < m_InitMenuSelection.y; ++i)
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));

        inputs[0].ki = { VK_RIGHT, 0, 0 };
        inputs[1].ki = { VK_RIGHT, 0, KEYEVENTF_KEYUP };

        for (LONG i = 0; i < m_InitMenuSelection.x; ++i)
            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));

        m_InitMenuSelection = {};
    }
}

LRESULT RootWindow::HandleMessage(const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    switch (uMsg)
    {
        HANDLE_MSG(WM_CREATE, OnCreate);
        HANDLE_MSG(WM_DESTROY, OnDestroy);
        HANDLE_MSG(WM_HOTKEY, OnHotKey);
        HANDLE_MSG(WM_INITMENUPOPUP, OnInitMenuPopup);
        HANDLE_DEF(Window::HandleMessage);
    }
}

bool Run(_In_ const LPCTSTR lpCmdLine, _In_ const int nShowCmd)
{
    if (RootWindow::Register() == 0)
    {
        MessageBox(NULL, TEXT("Error registering window class"), APPNAME, MB_ICONERROR | MB_OK);
        return false;
    }
    RootWindow* prw = RootWindow::Create();
    if (prw == nullptr)
    {
        MessageBox(NULL, TEXT("Error creating root window"), APPNAME, MB_ICONERROR | MB_OK);
        return false;
    }

    ShowWindow(*prw, nShowCmd);
    return true;
}
