// 09_window_main.inc
// WndProc and wWinMain entry point.
// Original lines: 5527-6085

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_uiThreadId = GetCurrentThreadId();
        g_hMain = hwnd;
        g_destroying = false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        LOGFONTW lf{};
        wcscpy_s(lf.lfFaceName, L"Segoe UI");

        lf.lfHeight = -15;
        lf.lfWeight = FW_NORMAL;
        g_hFont = CreateFontIndirectW(&lf);

        lf.lfHeight = -13;
        lf.lfWeight = FW_SEMIBOLD;
        g_hSmallFont = CreateFontIndirectW(&lf);

        lf.lfHeight = -11;
        lf.lfWeight = FW_SEMIBOLD;
        g_hTinyFont = CreateFontIndirectW(&lf);

        const wchar_t* tabs[] =
        {
            L"Мл.сотрудник",
            L"Сотрудник",
            L"Вед.сотрудник",
            L"Доп.программы"
        };

        for (int i = 0; i < TAB_COUNT; ++i)
        {
            HWND tab = CreateWindowExW(
                0,
                L"BUTTON",
                tabs[i],
                WS_CHILD | BS_OWNERDRAW,
                0, 0, 0, 0,
                hwnd,
                reinterpret_cast<HMENU>(IDC_TAB_BASE + i),
                g_hInst,
                nullptr
            );

            SendMessageW(tab, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            MakeButtonNoErase(tab);
            g_tabButtons.push_back(tab);
        }

        g_hDeleteHolyCheck = CreateWindowExW(
            0,
            L"BUTTON",
            L"Удалить",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_DELETE_HOLYCHECK),
            g_hInst,
            nullptr
        );

        SendMessageW(g_hDeleteHolyCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hDeleteHolyCheck);

        g_hCancelDownload = CreateWindowExW(
            0,
            L"BUTTON",
            L"Отмена",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_CANCEL_DOWNLOAD),
            g_hInst,
            nullptr
        );

        SendMessageW(g_hCancelDownload, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hCancelDownload);

        g_hDownloadAll = CreateWindowExW(
            0,
            L"BUTTON",
            L"Скачать все",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_DOWNLOAD_ALL),
            g_hInst,
            nullptr
        );

        SendMessageW(g_hDownloadAll, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hDownloadAll);

        g_hPowerShell = CreateWindowExW(
            0,
            L"BUTTON",
            L"PowerShell",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_POWERSHELL),
            g_hInst,
            nullptr
        );

        SendMessageW(g_hPowerShell, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hPowerShell);

        g_hEnableServices = CreateWindowExW(
            0,
            L"BUTTON",
            L"Вкл службы",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_ENABLE_SERVICES),
            g_hInst,
            nullptr
        );

        SendMessageW(g_hEnableServices, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hEnableServices);

        g_hCopyEverything1 = CreateWindowExW(0, L"BUTTON", L"Читы", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_COPY_EVERYTHING_1), g_hInst, nullptr);
        SendMessageW(g_hCopyEverything1, WM_SETFONT, reinterpret_cast<WPARAM>(g_hTinyFont), TRUE);
        MakeButtonNoErase(g_hCopyEverything1);

        g_hCopyEverything2 = CreateWindowExW(0, L"BUTTON", L"Конфиги", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_COPY_EVERYTHING_2), g_hInst, nullptr);
        SendMessageW(g_hCopyEverything2, WM_SETFONT, reinterpret_cast<WPARAM>(g_hTinyFont), TRUE);
        MakeButtonNoErase(g_hCopyEverything2);

        g_hCopyPowerShell = CreateWindowExW(0, L"BUTTON", L"Check", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_COPY_POWERSHELL), g_hInst, nullptr);
        SendMessageW(g_hCopyPowerShell, WM_SETFONT, reinterpret_cast<WPARAM>(g_hTinyFont), TRUE);
        MakeButtonNoErase(g_hCopyPowerShell);

        g_hCopySites = CreateWindowExW(0, L"BUTTON", L"Сайты", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_COPY_SITES), g_hInst, nullptr);
        SendMessageW(g_hCopySites, WM_SETFONT, reinterpret_cast<WPARAM>(g_hTinyFont), TRUE);
        MakeButtonNoErase(g_hCopySites);

        for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
        {
            HWND tile = CreateWindowExW(
                0,
                L"BUTTON",
                g_items[i].name.c_str(),
                WS_CHILD | BS_OWNERDRAW | BS_NOTIFY,
                0, 0, 0, 0,
                hwnd,
                reinterpret_cast<HMENU>(IDC_TILE_BASE + i),
                g_hInst,
                nullptr
            );

            SendMessageW(tile, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            MakeButtonNoErase(tile);
            g_tileButtons.push_back(tile);
        }

        g_itemBusy.assign(g_items.size(), 0);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        InitParticles(rc.right, rc.bottom);

        SetTimer(hwnd, TIMER_PARTICLES, 33, nullptr);
        LayoutControls(hwnd);

        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 980;
        info->ptMinTrackSize.y = 570;
        return 0;
    }

    case WM_SIZE:
    {
        if (wParam == SIZE_MINIMIZED)
        {
            g_particlesPaused = true;
            KillTimer(hwnd, TIMER_PARTICLES);
            return 0;
        }

        bool wasPaused = g_particlesPaused;
        g_particlesPaused = false;

        LayoutControls(hwnd);

        if (wasPaused || wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
        {
            RECT rc{};
            GetClientRect(hwnd, &rc);

            if (rc.right > 100 && rc.bottom > 100)
                InitParticles(static_cast<int>(rc.right), static_cast<int>(rc.bottom));

            SetTimer(hwnd, TIMER_PARTICLES, 33, nullptr);
        }

        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);
        return 0;
    }

    case WM_ENTERSIZEMOVE:
    {
        g_particlesPaused = true;
        KillTimer(hwnd, TIMER_PARTICLES);
        return 0;
    }

    case WM_EXITSIZEMOVE:
    {
        g_particlesPaused = false;
        SetTimer(hwnd, TIMER_PARTICLES, 33, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_PARTICLES)
        {
            if (!g_particlesPaused && !IsIconic(hwnd))
                UpdateParticles(hwnd);

            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
    {
        POINT pt{};
        pt.x = static_cast<SHORT>(LOWORD(lParam));
        pt.y = static_cast<SHORT>(HIWORD(lParam));
        ScreenToClient(hwnd, &pt);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT logRect = GetDownloadLogRect(rc);

        if (PointInRect(logRect, pt))
        {
            if (ScrollDownloadLog(GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                RECT invalidateRect{ 0, rc.bottom - LOG_INVALIDATE_HEIGHT, rc.right, rc.bottom };
                InvalidateRect(hwnd, &invalidateRect, FALSE);
            }

            return 0;
        }

        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_DOWNLOAD_ALL)
        {
            DownloadCurrentTab();
            return 0;
        }

        if (id == IDC_POWERSHELL)
        {
            OpenPowerShellMinecraft();
            return 0;
        }

        if (id == IDC_ENABLE_SERVICES)
        {
            OpenCmdEnableServices();
            return 0;
        }

        if (id == IDC_DELETE_HOLYCHECK)
        {
            DeleteHolyCheckFolder();
            return 0;
        }

        if (id == IDC_CANCEL_DOWNLOAD)
        {
            RequestCancelDownloads();
            return 0;
        }

        if (id == IDC_COPY_EVERYTHING_1)
        {
            CopyHelperText(EVERYTHING_QUERY_1, L"Everything 1");
            return 0;
        }

        if (id == IDC_COPY_EVERYTHING_2)
        {
            CopyHelperText(EVERYTHING_QUERY_2, L"Everything 2");
            return 0;
        }

        if (id == IDC_COPY_POWERSHELL)
        {
            CopyHelperText(POWERSHELL_TXT, L"PowerShell");
            return 0;
        }

        if (id == IDC_COPY_SITES)
        {
            CopyHelperText(POWERSHELL_SITES_TXT, L"Сайты");
            return 0;
        }

        if (id >= IDC_TAB_BASE && id < IDC_TAB_BASE + TAB_COUNT)
        {
            g_currentTab = id - IDC_TAB_BASE;
            LayoutControls(hwnd);

            return 0;
        }

        if (id >= IDC_TILE_BASE && id < IDC_TILE_BASE + static_cast<int>(g_items.size()))
        {
            if (code == BN_CLICKED)
                OpenOrDownloadSingleById(id);

            return 0;
        }

        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* d = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        if (d)
        {
            DrawRoundButton(d);
            return TRUE;
        }

        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        DrawScene(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_APP_REFRESH:
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT panelRect{ 0, rc.bottom - LOG_INVALIDATE_HEIGHT, rc.right, rc.bottom };
        InvalidateRect(hwnd, &panelRect, FALSE);
        return 0;
    }

    case WM_APP_PROGRESS:
    {
        g_progressPercent = static_cast<int>(wParam);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT progressRect{ 0, rc.bottom - LOG_INVALIDATE_HEIGHT, rc.right, rc.bottom };
        InvalidateRect(hwnd, &progressRect, FALSE);

        return 0;
    }

    case WM_APP_LOG:
    {
        std::wstring* line = reinterpret_cast<std::wstring*>(lParam);

        if (line)
        {
            PushLogLine(*line);
            delete line;
        }

        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT logRect{ 0, rc.bottom - LOG_INVALIDATE_HEIGHT, rc.right, rc.bottom };
        InvalidateRect(hwnd, &logRect, FALSE);

        return 0;
    }

    case WM_APP_ITEM_DONE:
    {
        int index = static_cast<int>(wParam);
        SetItemBusyIndex(index, false);

        if (!g_deleteInProgress.load())
            SetTileEnabledByIndex(index, true);

        InvalidateBottomPanel();
        return 0;
    }

    case WM_APP_DONE:
    {
        g_working = false;

        RECT rc{};
        GetClientRect(hwnd, &rc);
        RECT progressRect{ 0, rc.bottom - LOG_INVALIDATE_HEIGHT, rc.right, rc.bottom };

        if (g_deleteInProgress.load())
        {
            g_progressPercent = 0;
            InvalidateRect(hwnd, &progressRect, FALSE);
            return 0;
        }

        SetDownloadControlsEnabled(true);

        if (IsCancelRequested())
            g_progressPercent = 0;
        else
            g_progressPercent = 100;

        g_cancelRequested = false;

        InvalidateRect(hwnd, &progressRect, FALSE);
        return 0;
    }

    case WM_DESTROY:
    {
        g_destroying = true;
        g_cancelRequested = true;
        AbortActiveHttpHandles();
        KillTimer(hwnd, TIMER_PARTICLES);

        if (g_hFont)
        {
            DeleteObject(g_hFont);
            g_hFont = nullptr;
        }

        if (g_hSmallFont)
        {
            DeleteObject(g_hSmallFont);
            g_hSmallFont = nullptr;
        }

        if (g_hTinyFont)
        {
            DeleteObject(g_hTinyFont);
            g_hTinyFont = nullptr;
        }

        g_hMain = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HolyCheckDownloaderClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Не удалось зарегистрировать класс окна.", L"Ошибка", MB_ICONERROR);

        if (g_gdiplusToken)
            GdiplusShutdown(g_gdiplusToken);

        CoUninitialize();
        return 1;
    }

    g_hMain = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"holycheck <3",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1160,
        660,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hMain)
    {
        MessageBoxW(nullptr, L"Не удалось создать окно.", L"Ошибка", MB_ICONERROR);

        if (g_gdiplusToken)
            GdiplusShutdown(g_gdiplusToken);

        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_gdiplusToken)
        GdiplusShutdown(g_gdiplusToken);

    CoUninitialize();
    return 0;
}
