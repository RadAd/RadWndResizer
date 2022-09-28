#include "Window.h"
#include "Windowxx.h"
#include <tchar.h>
#include "HandlePtr.h"

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
};

void RootWindow::GetCreateWindow(CREATESTRUCT& cs)
{
    Window::GetCreateWindow(cs);
    cs.lpszName = APPNAME;
    cs.style = WS_OVERLAPPEDWINDOW;
}

BOOL RootWindow::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    RegisterHotKey(*this, HK_MENU, MOD_CONTROL | MOD_SHIFT, 'M');
    return TRUE;
}

void RootWindow::OnDestroy()
{
    PostQuitMessage(0);
}

HBITMAPPtr CreateGridBitmap(HDC hDC, SIZE sz, SIZE rowcols, POINT selected)
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

        if (selected.x >= 0 && selected.x < rowcols.cx && selected.y >= 0 && selected.y < rowcols.cy)
        {
            SetDCBrushColor(hMemDC, RGB(0, 0, 255));
            Rectangle(hMemDC, (sz.cx * selected.x) / rowcols.cx, (sz.cy * selected.y) / rowcols.cy, (sz.cx * (selected.x + 1)) / rowcols.cx, (sz.cy * (selected.y + 1)) / rowcols.cy);
        }
    }
    return hBitmap;
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

        int selected = -1;

        {
            HBITMAPPtr hBitmapSet[ARRAYSIZE(layouts)];
            {
                auto hDC = MakeGetDC(*this);
                for (int i = 0; i < ARRAYSIZE(layouts); ++i)
                    hBitmapSet[i] = CreateGridBitmap(hDC.get(), Size(r), layouts[i], POINT{ -1, -1 });
            }

            const UINT FIRST_CMD = 2;

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
                int i = 0;
                for (int x = 0; x < layout.cx; ++x)
                    for (int y = 0; y < layout.cy; ++y)
                        hBitmapSet[i++] = CreateGridBitmap(hDC.get(), Size(r), layout, POINT{ x, y });
            }

            const UINT FIRST_CMD = 2;

            HMENUPtr hMenu(CreatePopupMenu());
            for (UINT i = 0; i < count; ++i)
            {
                const bool dobreak = (i % (count / layout.cx)) == 0;
                AppendMenu(hMenu, MF_BITMAP | (dobreak ? MF_MENUBREAK : 0), FIRST_CMD + i, reinterpret_cast<LPCTSTR>(hBitmapSet[i].get()));
            }

            selected = TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, menupt.x, menupt.y, 0, *this, nullptr) - FIRST_CMD;

            if (selected >= 0 && selected < static_cast<int>(count))
            {
                POINT p = { selected / layout.cy, selected % layout.cy };
                RECT newr;
                newr.left = moninfo.rcWork.left + p.x * Width(moninfo.rcWork) / layout.cx;
                newr.right = newr.left + Width(moninfo.rcWork) / layout.cx;
                newr.top = moninfo.rcWork.top + p.y * Height(moninfo.rcWork) / layout.cy;
                newr.bottom = newr.top + Height(moninfo.rcWork) / layout.cy;
                InflateRect(&newr, fixborder, 0);
                newr.bottom += fixborder;
                MoveWindow(hWnd, newr.left, newr.top, Width(newr), Height(newr), TRUE);
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
