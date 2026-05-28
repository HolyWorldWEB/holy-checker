static LRESULT CALLBACK AutomationEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_MOUSEWHEEL)
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int target = 0;
        if (hwnd == g_hHistoryEdit) target = AUTOMATION_OUTPUT_HISTORYPW;
        else if (hwnd == g_hJarEdit) target = AUTOMATION_OUTPUT_JAR_ACTIVITY;
        else if (hwnd == g_hExeEdit) target = AUTOMATION_OUTPUT_EXE_DELETED;
        else if (hwnd == g_hPfEdit) target = AUTOMATION_OUTPUT_PF_DELETED;
        else if (hwnd == g_hMcEdit) target = AUTOMATION_OUTPUT_MC_LOGS;

        if (target != 0 && ScrollAutomationOutput(target, delta))
        {
            SyncAutomationEditScrollToState(target);
            InvalidateAutomationPanel(target);
        }
        return 0;
    }

    if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (wParam == L'A' || wParam == L'a'))
    {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }

    if (g_oldAutomationEditProc)
        return CallWindowProcW(g_oldAutomationEditProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void SubclassAutomationEdit(HWND edit)
{
    if (!edit)
        return;
    WNDPROC oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AutomationEditProc)));
    if (!g_oldAutomationEditProc && oldProc && oldProc != AutomationEditProc)
        g_oldAutomationEditProc = oldProc;
}

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

        wcscpy_s(lf.lfFaceName, L"Consolas");
        lf.lfHeight = -10;
        lf.lfWeight = FW_SEMIBOLD;
        g_hAutomationTextFont = CreateFontIndirectW(&lf);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");

        g_hAutomationEditBrush = CreateSolidBrush(RGB(20, 22, 28));

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
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_BASE + i)),
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

        g_hAutomationNav = CreateWindowExW(0, L"BUTTON", L"check", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_NAV), g_hInst, nullptr);
        SendMessageW(g_hAutomationNav, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationNav);

        g_hHomeNav = CreateWindowExW(0, L"BUTTON", L"Главная", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_HOME_NAV), g_hInst, nullptr);
        SendMessageW(g_hHomeNav, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hHomeNav);

        g_hAutomationBam = CreateWindowExW(0, L"BUTTON", L"bam", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_BAM), g_hInst, nullptr);
        SendMessageW(g_hAutomationBam, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationBam);

        g_hAutomationEventVwr = CreateWindowExW(0, L"BUTTON", L"eventvwr", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_EVENTVWR), g_hInst, nullptr);
        SendMessageW(g_hAutomationEventVwr, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationEventVwr);

        g_hAutomationBamUnsignedOnly = CreateWindowExW(0, L"BUTTON", L"not signed only", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_BAM_UNSIGNED_ONLY), g_hInst, nullptr);
        SendMessageW(g_hAutomationBamUnsignedOnly, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSmallFont ? g_hSmallFont : g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationBamUnsignedOnly);

        g_hAutomationHistoryPw = CreateWindowExW(0, L"BUTTON", L"historypw", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_HISTORYPW), g_hInst, nullptr);
        SendMessageW(g_hAutomationHistoryPw, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationHistoryPw);

        g_hHistoryEdit = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL | ES_WANTRETURN,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_HISTORYPW_EDIT),
            g_hInst,
            nullptr
        );
        SendMessageW(g_hHistoryEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hAutomationTextFont ? g_hAutomationTextFont : g_hSmallFont), TRUE);
        SendMessageW(g_hHistoryEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SubclassAutomationEdit(g_hHistoryEdit);

        auto createScanEdit = [&](int id) -> HWND {
            HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL | ES_WANTRETURN, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(id), g_hInst, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hAutomationTextFont ? g_hAutomationTextFont : g_hSmallFont), TRUE);
            SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
            SubclassAutomationEdit(h);
            return h;
        };
        g_hJarEdit = createScanEdit(IDC_JAR_EDIT);
        g_hExeEdit = createScanEdit(IDC_EXE_EDIT);
        g_hPfEdit = createScanEdit(IDC_PF_EDIT);
        g_hMcEdit = createScanEdit(IDC_MC_EDIT);

        g_hAutomationJarPage = CreateWindowExW(0, L"BUTTON", L"2 лист", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_JAR_PAGE), g_hInst, nullptr);
        SendMessageW(g_hAutomationJarPage, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationJarPage);

        g_hJarActiv = CreateWindowExW(0, L"BUTTON", L".jar activ", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_JAR_ACTIV), g_hInst, nullptr);
        SendMessageW(g_hJarActiv, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hJarActiv);

        g_hExeDeleted = CreateWindowExW(0, L"BUTTON", L".exe activ", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_EXE_DELETED), g_hInst, nullptr);
        SendMessageW(g_hExeDeleted, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hExeDeleted);

        g_hPfDeleted = CreateWindowExW(0, L"BUTTON", L".pf del", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PF_DELETED), g_hInst, nullptr);
        SendMessageW(g_hPfDeleted, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hPfDeleted);

        g_hMcLogs = CreateWindowExW(0, L"BUTTON", L"mc logs", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_MC_LOGS), g_hInst, nullptr);
        SendMessageW(g_hMcLogs, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hMcLogs);

        g_hPage4Nav = CreateWindowExW(0, L"BUTTON", L"3 лист", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PAGE4_NAV), g_hInst, nullptr);
        SendMessageW(g_hPage4Nav, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hPage4Nav);

        g_hModHash = CreateWindowExW(0, L"BUTTON", L"mod hash", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_MOD_HASH), g_hInst, nullptr);
        SendMessageW(g_hModHash, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hModHash);

        g_hDllUnsigned = CreateWindowExW(0, L"BUTTON", L"unsigned dll", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_DLL_UNSIGNED), g_hInst, nullptr);
        SendMessageW(g_hDllUnsigned, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hDllUnsigned);

        g_hPage5Nav = CreateWindowExW(0, L"BUTTON", L"4 лист", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PAGE5_NAV), g_hInst, nullptr);
        SendMessageW(g_hPage5Nav, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hPage5Nav);

        g_hUsbChecker = CreateWindowExW(0, L"BUTTON", L"usb", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_USB_CHECKER), g_hInst, nullptr);
        SendMessageW(g_hUsbChecker, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hUsbChecker);

        g_hInjGenCheck = CreateWindowExW(0, L"BUTTON", L"injgen", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_INJGEN_CHECK), g_hInst, nullptr);
        SendMessageW(g_hInjGenCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hInjGenCheck);

        for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
        {
            HWND tile = CreateWindowExW(
                0,
                L"BUTTON",
                g_items[i].name.c_str(),
                WS_CHILD | BS_OWNERDRAW | BS_NOTIFY,
                0, 0, 0, 0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TILE_BASE + i)),
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

        SetTimer(hwnd, TIMER_PARTICLES, 90, nullptr);
        LayoutControls(hwnd);

        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 980;
        info->ptMinTrackSize.y = 680;
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

            SetTimer(hwnd, TIMER_PARTICLES, 90, nullptr);
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
        SetTimer(hwnd, TIMER_PARTICLES, 90, nullptr);
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

    case WM_LBUTTONDOWN:
    {
        POINT pt{};
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        int target = AutomationScrollTargetAtPoint(pt);
        if (target != 0)
        {
            g_automationScrollDragging = true;
            g_automationScrollDragTarget = target;
            SetCapture(hwnd);
            SetAutomationScrollFromPoint(target, pt);
            SyncAutomationEditScrollToState(target);
            InvalidateAutomationPanel(target);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (g_automationScrollDragging && g_automationScrollDragTarget != 0)
        {
            POINT pt{};
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            if (SetAutomationScrollFromPoint(g_automationScrollDragTarget, pt))
            {
                SyncAutomationEditScrollToState(g_automationScrollDragTarget);
                InvalidateAutomationPanel(g_automationScrollDragTarget);
            }
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (g_automationScrollDragging)
        {
            POINT pt{};
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            if (g_automationScrollDragTarget != 0 && SetAutomationScrollFromPoint(g_automationScrollDragTarget, pt))
            {
                SyncAutomationEditScrollToState(g_automationScrollDragTarget);
                InvalidateAutomationPanel(g_automationScrollDragTarget);
            }
            g_automationScrollDragging = false;
            g_automationScrollDragTarget = 0;
            ReleaseCapture();
            return 0;
        }
        break;
    }

    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    {
        g_automationScrollDragging = false;
        g_automationScrollDragTarget = 0;
        break;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt{};
        pt.x = static_cast<SHORT>(LOWORD(lParam));
        pt.y = static_cast<SHORT>(HIWORD(lParam));
        ScreenToClient(hwnd, &pt);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        if (g_currentPage == 1)
        {
            RECT bamPanel = GetAutomationBamPanelRect(rc);
            RECT eventPanel = GetAutomationEventPanelRect(rc);
            RECT historyPanel = GetAutomationHistoryPanelRect(rc);

            if (PointInRect(bamPanel, pt) && ScrollAutomationOutput(AUTOMATION_OUTPUT_BAM, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_BAM);
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_BAM);
                return 0;
            }

            if (PointInRect(eventPanel, pt) && ScrollAutomationOutput(AUTOMATION_OUTPUT_EVENTVWR, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_EVENTVWR);
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_EVENTVWR);
                return 0;
            }

            if (PointInRect(historyPanel, pt) && ScrollAutomationOutput(AUTOMATION_OUTPUT_HISTORYPW, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_HISTORYPW);
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_HISTORYPW);
                return 0;
            }

            return 0;
        }

        if (g_currentPage == 2)
        {
            struct WheelPanel { RECT rc; int target; };
            WheelPanel panels[] = {
                { GetJarActivityPanelRect(rc), AUTOMATION_OUTPUT_JAR_ACTIVITY },
                { GetExeDeletedPanelRect(rc), AUTOMATION_OUTPUT_EXE_DELETED },
                { GetPfDeletedPanelRect(rc), AUTOMATION_OUTPUT_PF_DELETED }
            };
            for (const WheelPanel& p : panels)
            {
                if (PointInRect(p.rc, pt) && ScrollAutomationOutput(p.target, GET_WHEEL_DELTA_WPARAM(wParam)))
                {
                    SyncAutomationEditScrollToState(p.target);
                    InvalidateAutomationPanel(p.target);
                    return 0;
                }
            }
            return 0;
        }

        if (g_currentPage == 3)
        {
            struct WheelPanel { RECT rc; int target; };
            WheelPanel panels[] = {
                { GetPage4PanelRect(rc), AUTOMATION_OUTPUT_PAGE4 },
                { GetPage4DllPanelRect(rc), AUTOMATION_OUTPUT_DLL_UNSIGNED }
            };
            for (const WheelPanel& p : panels)
            {
                if (PointInRect(p.rc, pt) && ScrollAutomationOutput(p.target, GET_WHEEL_DELTA_WPARAM(wParam)))
                {
                    SyncAutomationEditScrollToState(p.target);
                    InvalidateAutomationPanel(p.target);
                    return 0;
                }
            }
            return 0;
        }

        if (g_currentPage == 4)
        {
            struct WheelPanel { RECT rc; int target; };
            WheelPanel panels[] = {
                { GetPage5PanelRect(rc), AUTOMATION_OUTPUT_PAGE5 },
                { GetPage5InjGenPanelRect(rc), AUTOMATION_OUTPUT_INJGEN }
            };
            for (const WheelPanel& p : panels)
            {
                if (PointInRect(p.rc, pt) && ScrollAutomationOutput(p.target, GET_WHEEL_DELTA_WPARAM(wParam)))
                {
                    SyncAutomationEditScrollToState(p.target);
                    InvalidateAutomationPanel(p.target);
                    return 0;
                }
            }
            return 0;
        }

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

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        if (reinterpret_cast<HWND>(lParam) == g_hHistoryEdit || reinterpret_cast<HWND>(lParam) == g_hJarEdit || reinterpret_cast<HWND>(lParam) == g_hExeEdit || reinterpret_cast<HWND>(lParam) == g_hPfEdit || reinterpret_cast<HWND>(lParam) == g_hMcEdit)
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, TEXT);
            SetBkColor(hdc, RGB(20, 22, 28));
            return reinterpret_cast<LRESULT>(g_hAutomationEditBrush ? g_hAutomationEditBrush : GetStockObject(BLACK_BRUSH));
        }
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_HOME_NAV)
        {
            g_currentPage = 0;
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_AUTOMATION_NAV)
        {
            g_currentPage = 1;
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_AUTOMATION_JAR_PAGE)
        {
            g_currentPage = 2;
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_PAGE4_NAV)
        {
            g_currentPage = 3;
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_PAGE5_NAV)
        {
            g_currentPage = 4;
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_AUTOMATION_BAM)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_BAM);
            return 0;
        }

        if (id == IDC_AUTOMATION_BAM_UNSIGNED_ONLY)
        {
            bool now = !g_bamUnsignedOnly.load();
            g_bamUnsignedOnly.store(now);
            InvalidateRect(g_hAutomationBamUnsignedOnly, nullptr, TRUE);
            if (g_currentPage == 1)
            {
                SetAutomationStatusLine(AUTOMATION_OUTPUT_BAM, now ? L"включён фильтр not signed only. Нажмите bam для сканирования." : L"фильтр not signed only выключен. Нажмите bam для сканирования.");
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_BAM);
            }
            return 0;
        }

        if (id == IDC_AUTOMATION_EVENTVWR)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_EVENTVWR);
            return 0;
        }

        if (id == IDC_AUTOMATION_HISTORYPW)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_HISTORYPW);
            return 0;
        }

        if (id == IDC_JAR_ACTIV)
        {
            g_activityScanMode.store(0);
            StartAutomationScan(AUTOMATION_OUTPUT_JAR_ACTIVITY);
            return 0;
        }

        if (id == IDC_EXE_DELETED)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_EXE_DELETED);
            return 0;
        }

        if (id == IDC_PF_DELETED)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_PF_DELETED);
            return 0;
        }

        if (id == IDC_MC_LOGS)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_MC_LOGS);
            return 0;
        }

        if (id == IDC_MOD_HASH)
        {
            g_page4ScanMode.store(0);
            StartAutomationScan(AUTOMATION_OUTPUT_PAGE4);
            return 0;
        }

        if (id == IDC_DLL_UNSIGNED)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_DLL_UNSIGNED);
            return 0;
        }

        if (id == IDC_USB_CHECKER)
        {
            g_page5ScanMode.store(0);
            StartAutomationScan(AUTOMATION_OUTPUT_PAGE5);
            return 0;
        }

        if (id == IDC_INJGEN_CHECK)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_INJGEN);
            return 0;
        }

        if (g_currentPage >= 1 && g_currentPage <= 4)
            return 0;

        if (id == IDC_DOWNLOAD_ALL)
        {
            DownloadCurrentTab();
            return 0;
        }

        if (id == IDC_POWERSHELL)
        {
            CopyHelperText(POWERSHELL_TXT, L"PowerShell");
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

    case WM_APP_AUTOMATION_DONE:
    {
        AutomationResult* result = reinterpret_cast<AutomationResult*>(lParam);

        if (result)
        {
            int finishedTarget = result->target;
            SetAutomationLines(finishedTarget, std::move(result->lines));
            if (AutomationEditForTarget(finishedTarget))
                UpdateAutomationEditWindow(finishedTarget);
            InvalidateAutomationPanel(finishedTarget);
            AutomationWorkingRef(finishedTarget).store(false);
            delete result;
        }

        InvalidateRect(g_hAutomationBam, nullptr, TRUE);
        InvalidateRect(g_hAutomationBamUnsignedOnly, nullptr, TRUE);
        InvalidateRect(g_hAutomationEventVwr, nullptr, TRUE);
        InvalidateRect(g_hAutomationHistoryPw, nullptr, TRUE);
        InvalidateRect(g_hAutomationJarPage, nullptr, TRUE);
        InvalidateRect(g_hJarActiv, nullptr, TRUE);
        InvalidateRect(g_hExeDeleted, nullptr, TRUE);
        InvalidateRect(g_hPfDeleted, nullptr, TRUE);
        InvalidateRect(g_hMcLogs, nullptr, TRUE);
        InvalidateRect(g_hPage4Nav, nullptr, TRUE);
        InvalidateRect(g_hModHash, nullptr, TRUE);
        InvalidateRect(g_hDllUnsigned, nullptr, TRUE);
        InvalidateRect(g_hPage5Nav, nullptr, TRUE);
        InvalidateRect(g_hUsbChecker, nullptr, TRUE);
        InvalidateRect(g_hInjGenCheck, nullptr, TRUE);
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

        if (g_hAutomationTextFont)
        {
            DeleteObject(g_hAutomationTextFont);
            g_hAutomationTextFont = nullptr;
        }

        if (g_hAutomationEditBrush)
        {
            DeleteObject(g_hAutomationEditBrush);
            g_hAutomationEditBrush = nullptr;
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

    if (!IsRunningAsAdministrator())
    {
        if (RelaunchAsAdministrator())
            return 0;

        MessageBoxW(nullptr, L"Программу нужно запускать от имени администратора.", L"Требуются права администратора", MB_ICONERROR | MB_OK);
        return 1;
    }

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
        1120,
        760,
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
