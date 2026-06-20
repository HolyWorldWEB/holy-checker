static int AutomationTargetFromEdit(HWND hwnd)
{
    if (hwnd == g_hEventEdit) return AUTOMATION_OUTPUT_EVENTVWR;
    if (hwnd == g_hJarEdit) return AUTOMATION_OUTPUT_JAR_ACTIVITY;
    if (hwnd == g_hExeEdit) return AUTOMATION_OUTPUT_EXE_DELETED;
    if (hwnd == g_hPfEdit) return AUTOMATION_OUTPUT_PF_DELETED;
    if (hwnd == g_hMcEdit) return AUTOMATION_OUTPUT_MC_LOGS;
    return 0;
}

static std::wstring AutomationPathFromLineForCopy(int target, const std::wstring& line);
static std::wstring AutomationNameFromLineForCopy(int target, const std::wstring& line);
static void ShowAutomationCopyPathMenu(HWND hwnd, POINT screenPt, const std::wstring& path, int highlightTarget, size_t highlightIndex, const std::wstring& name = L"");

static int AutomationEditLineFromClientPoint(HWND hwnd, POINT clientPt, size_t lineCount)
{
    if (!hwnd || lineCount == 0)
        return -1;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (clientPt.y < 0 || clientPt.y >= (rc.bottom - rc.top))
        return -1;

    LRESULT hit = SendMessageW(hwnd, EM_CHARFROMPOS, 0, MAKELPARAM(clientPt.x, clientPt.y));
    int lineIndex = static_cast<int>(HIWORD(hit));

    // EM_CHARFROMPOS у многострочного EDIT учитывает реальную прокрутку,
    // внутренние отступы и высоту шрифта. Поэтому ПКМ больше не перескакивает
    // на соседнюю строку при клике выше/ниже середины строки.
    if (lineIndex < 0)
        return -1;
    if (static_cast<size_t>(lineIndex) >= lineCount)
        return -1;

    return lineIndex;
}

static LRESULT CALLBACK AutomationEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int editTarget = AutomationTargetFromEdit(hwnd);

    if (msg == WM_VSCROLL)
    {
        LRESULT result = g_oldAutomationEditProc ? CallWindowProcW(g_oldAutomationEditProc, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
        int target = AutomationTargetFromEdit(hwnd);
        if (target != 0)
        {
            int first = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
            {
                std::lock_guard<std::mutex> lock(g_automationMutex);
                int& scroll = AutomationScrollRef(target);
                scroll = std::max<int>(0, first);
                ClampAutomationScrollOffsetLocked(target);
            }
            InvalidateAutomationPanel(target);
        }
        return result;
    }

    if (msg == WM_MOUSEWHEEL)
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int target = AutomationTargetFromEdit(hwnd);

        if (target != 0 && ScrollAutomationOutput(target, delta))
        {
            SyncAutomationEditScrollToState(target);
            InvalidateAutomationPanel(target);
        }
        return 0;
    }

    if (msg == WM_CONTEXTMENU || msg == WM_RBUTTONUP)
    {
        int target = AutomationTargetFromEdit(hwnd);
        if (target != 0)
        {
            POINT screenPt{};
            if (msg == WM_CONTEXTMENU)
            {
                if (lParam == -1)
                    GetCursorPos(&screenPt);
                else
                {
                    screenPt.x = GET_X_LPARAM(lParam);
                    screenPt.y = GET_Y_LPARAM(lParam);
                }
            }
            else
            {
                screenPt.x = GET_X_LPARAM(lParam);
                screenPt.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &screenPt);
            }

            POINT clientPt = screenPt;
            ScreenToClient(hwnd, &clientPt);

            std::vector<std::wstring> lines;
            int stateScroll = 0;
            GetAutomationSnapshot(target, lines, stateScroll);

            int lineIndex = AutomationEditLineFromClientPoint(hwnd, clientPt, lines.size());

            if (lineIndex >= 0 && static_cast<size_t>(lineIndex) < lines.size())
            {
                LRESULT charStart = SendMessageW(hwnd, EM_LINEINDEX, static_cast<WPARAM>(lineIndex), 0);
                if (charStart >= 0)
                {
                    LRESULT lineLen = SendMessageW(hwnd, EM_LINELENGTH, static_cast<WPARAM>(charStart), 0);
                    SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(charStart), static_cast<LPARAM>(charStart + std::max<LRESULT>(0, lineLen)));
                }

                const std::wstring& line = lines[static_cast<size_t>(lineIndex)];
                std::wstring path = AutomationPathFromLineForCopy(target, line);
                std::wstring name = AutomationNameFromLineForCopy(target, line);
                if (!path.empty() || !name.empty())
                {
                    ShowAutomationCopyPathMenu(hwnd, screenPt, path, target, static_cast<size_t>(lineIndex), name);
                    return 0;
                }
            }
        }
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

static int AutomationActivityHeaderColumnAtPoint(HWND hwnd, POINT pt, int target)
{
    if (!hwnd || g_currentPage != 2)
        return -1;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT panel = (target == AUTOMATION_OUTPUT_DLL_ACTIVITY) ? GetDllActivityPanelRect(rc) : GetJarActivityPanelRect(rc);
    if (!PointInRect(panel, pt))
        return -1;

    // Header row is drawn as the first row at panel.top + 30. Only the two
    // date/action columns are sortable.
    const int headerTop = panel.top + 30;
    const int headerBottom = headerTop + 15;
    if (pt.y < headerTop || pt.y > headerBottom)
        return -1;

    std::vector<std::wstring> lines;
    int scrollOffset = 0;
    GetAutomationSnapshot(target, lines, scrollOffset);
    if (lines.empty() || scrollOffset != 0)
        return -1;

    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return -1;

    Graphics g(hdc);
    FontFamily family(L"Consolas");
    Font lineFont(&family, 13.0f, FontStyleRegular, UnitPixel);
    std::vector<float> widths = ComputeAutomationColumnWidths(g, lines, 0, lines.size(), target, lineFont, static_cast<float>(panel.right - panel.left));
    ApplyAutomationColumnWidthOverrides(target, widths);
    ReleaseDC(hwnd, hdc);

    if (widths.size() < 2)
        return -1;

    const float sepW = 6.0f;
    float cur = static_cast<float>(panel.left) + 10.0f;
    for (size_t c = 0; c < widths.size(); ++c)
    {
        float left = cur;
        float right = cur + widths[c];
        if (pt.x >= static_cast<int>(left) && pt.x <= static_cast<int>(right))
        {
            if (c == 0 || c == 1)
                return static_cast<int>(c);
            return -1;
        }
        cur = right + sepW;
    }

    return -1;
}

static int AutomationJarActivityHeaderColumnAtPoint(HWND hwnd, POINT pt)
{
    return AutomationActivityHeaderColumnAtPoint(hwnd, pt, AUTOMATION_OUTPUT_JAR_ACTIVITY);
}

static int AutomationDllActivityHeaderColumnAtPoint(HWND hwnd, POINT pt)
{
    return AutomationActivityHeaderColumnAtPoint(hwnd, pt, AUTOMATION_OUTPUT_DLL_ACTIVITY);
}


static const UINT IDM_AUTOMATION_COPY_PATH = 56001;
static const UINT IDM_AUTOMATION_COPY_NAME = 56002;

static bool AutomationPanelAtPointForContext(HWND hwnd, POINT pt, RECT& outPanel, int& outTarget)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);

    struct PanelHit { RECT rc; int target; };
    std::vector<PanelHit> panels;

    if (g_currentPage == 1)
    {
        panels.push_back({ GetAutomationBamPanelRect(rc), AUTOMATION_OUTPUT_PAGE4 });
        panels.push_back({ GetAutomationEventPanelRect(rc), AUTOMATION_OUTPUT_EVENTVWR });
    }
    else if (g_currentPage == 2)
    {
        panels.push_back({ GetJarActivityPanelRect(rc), AUTOMATION_OUTPUT_JAR_ACTIVITY });
        panels.push_back({ GetDllActivityPanelRect(rc), AUTOMATION_OUTPUT_DLL_ACTIVITY });
        panels.push_back({ GetDllUnsignedPanelRect(rc), AUTOMATION_OUTPUT_DLL_UNSIGNED });
    }
    else if (g_currentPage == 3)
    {
        panels.push_back({ GetFullAutomationPanelRect(rc), AUTOMATION_OUTPUT_PAGE4 });
    }
    else if (g_currentPage == 4)
    {
        panels.push_back({ GetPage4PanelRect(rc), AUTOMATION_OUTPUT_PAGE4 });
        panels.push_back({ GetPage4DllPanelRect(rc), AUTOMATION_OUTPUT_DLL_UNSIGNED });
    }
    else if (g_currentPage == 5)
    {
        panels.push_back({ GetPage5InjGenPanelRect(rc), AUTOMATION_OUTPUT_INJGEN });
    }

    for (const PanelHit& p : panels)
    {
        if (PointInRect(p.rc, pt))
        {
            outPanel = p.rc;
            outTarget = p.target;
            return true;
        }
    }

    outTarget = 0;
    SetRectEmpty(&outPanel);
    return false;
}

static bool IsPathLikeAutomationCell(const std::wstring& cell)
{
    std::wstring lower = ToLowerCopy(StripAutomationDeletedMarker(cell));
    return lower.find(L":\\") != std::wstring::npos ||
           lower.find(L"\\device\\") != std::wstring::npos ||
           lower.find(L".exe") != std::wstring::npos ||
           lower.find(L".jar") != std::wstring::npos ||
           lower.find(L".dll") != std::wstring::npos ||
           lower.find(L".pf") != std::wstring::npos;
}

static bool IsFullPathLikeAutomationCell(const std::wstring& cell)
{
    std::wstring lower = ToLowerCopy(StripAutomationDeletedMarker(cell));
    return lower.find(L":\\") != std::wstring::npos ||
           lower.rfind(L"\\\\", 0) == 0 ||
           lower.find(L"\\device\\") != std::wstring::npos;
}

static std::wstring ExtractAutomationPathFromProgramCell(const std::wstring& cell)
{
    std::wstring value = TrimAutomationField(StripAutomationDeletedMarker(cell));
    if (value.empty())
        return L"";

    std::wstring lower = ToLowerCopy(value);
    size_t start = lower.find(L":\\");
    if (start != std::wstring::npos && start > 0)
    {
        // Move back to the drive letter. In a cell like "app.exe    C:\path\app.exe"
        // find(":\") points at ':', so the path begins one char earlier.
        start -= 1;
        return TrimAutomationField(value.substr(start));
    }

    start = lower.find(L"\\device\\");
    if (start != std::wstring::npos)
        return TrimAutomationField(value.substr(start));

    return IsPathLikeAutomationCell(value) ? value : L"";
}

static std::wstring AutomationPathFromLineForCopy(int target, const std::wstring& line)
{
    std::wstring cleanLine = StripAutomationDeletedMarker(line);
    if (cleanLine.empty() || cleanLine == L"не найдено" || cleanLine.find(L"не удалось") != std::wstring::npos)
        return L"";

    std::vector<std::wstring> cols = SplitAutomationPipeColumns(cleanLine);
    if (cols.empty())
        return IsPathLikeAutomationCell(cleanLine) ? ExtractAutomationPathFromProgramCell(cleanLine) : L"";

    // .jar activity / .exe activity: date | program | path.
    // Copy path must not copy the program name when the path column contains only
    // a filename fallback.  Offer Copy path only for a real full path.
    if (target == AUTOMATION_OUTPUT_EXE_DELETED && cols.size() >= 3)
    {
        std::wstring path = ExtractAutomationPathFromProgramCell(cols[2]);
        return IsFullPathLikeAutomationCell(path) ? path : L"";
    }

    // BAM-подобная строка: дата | программа | путь | подпись
    if (target == AUTOMATION_OUTPUT_BAM && cols.size() >= 3)
    {
        std::wstring path = ExtractAutomationPathFromProgramCell(cols[2]);
        return IsFullPathLikeAutomationCell(path) ? path : L"";
    }

    // checkmods / unsigned dll обычно: имя/дата | путь...
    if ((target == AUTOMATION_OUTPUT_PAGE4 || target == AUTOMATION_OUTPUT_DLL_UNSIGNED) && cols.size() >= 2)
    {
        for (size_t i = 1; i < cols.size(); ++i)
        {
            std::wstring path = ExtractAutomationPathFromProgramCell(cols[i]);
            if (IsFullPathLikeAutomationCell(path))
                return path;
        }
    }

    // deleted/activity окна: путь обычно в последней колонке.
    if ((target == AUTOMATION_OUTPUT_MC_LOGS || target == AUTOMATION_OUTPUT_PF_DELETED || target == AUTOMATION_OUTPUT_INJGEN) && cols.size() >= 2)
    {
        for (size_t i = cols.size(); i-- > 0; )
        {
            std::wstring path = ExtractAutomationPathFromProgramCell(cols[i]);
            if (IsFullPathLikeAutomationCell(path))
                return path;
        }
    }

    for (const std::wstring& col : cols)
    {
        std::wstring path = ExtractAutomationPathFromProgramCell(col);
        if (IsFullPathLikeAutomationCell(path))
            return path;
    }

    return L"";
}

static std::wstring AutomationNameFromLineForCopy(int target, const std::wstring& line)
{
    std::wstring cleanLine = StripAutomationDeletedMarker(line);
    if (cleanLine.empty() || cleanLine == L"не найдено" || cleanLine.find(L"не удалось") != std::wstring::npos)
        return L"";

    std::vector<std::wstring> cols = SplitAutomationPipeColumns(cleanLine);
    if (cols.empty())
        return L"";

    // checkmods / unsigned dll: first column is the displayed file name.
    if ((target == AUTOMATION_OUTPUT_PAGE4 || target == AUTOMATION_OUTPUT_DLL_UNSIGNED) && cols.size() >= 1)
        return TrimAutomationField(cols[0]);

    // BAM: date | program | path | signature.
    if (target == AUTOMATION_OUTPUT_BAM && cols.size() >= 2)
        return TrimAutomationField(cols[1]);

    // .jar activity: modified date | reason (journal date) | program.
    if ((target == AUTOMATION_OUTPUT_JAR_ACTIVITY || target == AUTOMATION_OUTPUT_DLL_ACTIVITY) && cols.size() >= 3)
        return TrimAutomationField(cols[2]);

    // .exe activity: date | program | path.
    if (target == AUTOMATION_OUTPUT_EXE_DELETED && cols.size() >= 2)
        return TrimAutomationField(cols[1]);

    return L"";
}

static bool AutomationPathAtPointForCopy(HWND hwnd, POINT clientPt, std::wstring& outPath, int* outTarget = nullptr, size_t* outIndex = nullptr, std::wstring* outName = nullptr)
{
    outPath.clear();
    if (outTarget) *outTarget = 0;
    if (outIndex) *outIndex = static_cast<size_t>(-1);
    if (outName) outName->clear();

    RECT panel{};
    int target = 0;
    if (!AutomationPanelAtPointForContext(hwnd, clientPt, panel, target))
        return false;

    const float lineH = 15.0f;
    const float firstLineY = static_cast<float>(panel.top) + 30.0f;
    if (static_cast<float>(clientPt.y) < firstLineY)
        return false;

    std::vector<std::wstring> lines;
    int scrollOffset = 0;
    GetAutomationSnapshot(target, lines, scrollOffset);
    if (lines.empty())
        return false;

    size_t capacity = static_cast<size_t>(std::max(1.0f, (panel.bottom - panel.top - 34.0f) / lineH));
    float relY = static_cast<float>(clientPt.y) - firstLineY;
    if (relY < 0.0f)
        return false;
    size_t row = static_cast<size_t>(relY / lineH);
    if (row >= capacity)
        return false;

    size_t index = static_cast<size_t>(std::max(0, scrollOffset)) + row;
    if (index >= lines.size())
        return false;

    outPath = AutomationPathFromLineForCopy(target, lines[index]);
    std::wstring name = AutomationNameFromLineForCopy(target, lines[index]);
    if (outName) *outName = name;
    if (!outPath.empty() || !name.empty())
    {
        if (outTarget) *outTarget = target;
        if (outIndex) *outIndex = index;
        return true;
    }
    return false;
}

static void ShowAutomationCopyPathMenu(HWND hwnd, POINT screenPt, const std::wstring& path, int highlightTarget, size_t highlightIndex, const std::wstring& name)
{
    if (path.empty() && name.empty())
        return;

    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    if (highlightTarget != 0 && highlightIndex != static_cast<size_t>(-1))
    {
        g_automationCopyHighlightTarget = highlightTarget;
        g_automationCopyHighlightIndex = highlightIndex;
        InvalidateAutomationPanel(highlightTarget);
        UpdateWindow(hwnd);
    }

    if (!path.empty())
        AppendMenuW(menu, MF_STRING, IDM_AUTOMATION_COPY_PATH, name.empty() ? L"Copy" : L"Copy path");
    if (!name.empty())
        AppendMenuW(menu, MF_STRING, IDM_AUTOMATION_COPY_NAME, path.empty() ? L"Copy" : L"Copy name");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (g_automationCopyHighlightTarget != 0)
    {
        int oldTarget = g_automationCopyHighlightTarget;
        g_automationCopyHighlightTarget = 0;
        g_automationCopyHighlightIndex = static_cast<size_t>(-1);
        InvalidateAutomationPanel(oldTarget);
    }

    if (cmd == IDM_AUTOMATION_COPY_PATH && !path.empty())
        CopyTextToClipboard(path.c_str());
    else if (cmd == IDM_AUTOMATION_COPY_NAME && !name.empty())
        CopyTextToClipboard(name.c_str());
}

static void ResizeMainWindowForPage(int page);

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
        lf.lfHeight = -13;
        lf.lfWeight = FW_NORMAL;
        g_hAutomationTextFont = CreateFontIndirectW(&lf);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");

        g_hAutomationEditBrush = CreateSolidBrush(RGB(23, 24, 27));

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

        g_hAutomationBamUnsignedOnly = CreateWindowExW(0, L"BUTTON", L"unsigned only", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_BAM_UNSIGNED_ONLY), g_hInst, nullptr);
        SendMessageW(g_hAutomationBamUnsignedOnly, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSmallFont ? g_hSmallFont : g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationBamUnsignedOnly);


        auto createScanEdit = [&](int id, bool hideNativeScrollbars = false, bool transparentBackground = false) -> HWND {
            DWORD style = WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL | ES_WANTRETURN | ES_AUTOHSCROLL;
            // ES_AUTOHSCROLL + WS_HSCROLL keep one log event on one physical line.
            // For eventvwr the native scrollbars are hidden after create,
            // so no white bars are visible, but the EDIT does not word-wrap labels/details.
            style |= WS_VSCROLL | WS_HSCROLL;

            UNREFERENCED_PARAMETER(transparentBackground);
            DWORD exStyle = 0;
            HWND h = CreateWindowExW(
                exStyle,
                L"EDIT",
                L"",
                style,
                0, 0, 0, 0,
                hwnd,
                reinterpret_cast<HMENU>(id),
                g_hInst,
                nullptr
            );
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hAutomationTextFont ? g_hAutomationTextFont : g_hSmallFont), TRUE);
            SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
            if (hideNativeScrollbars)
                ShowScrollBar(h, SB_BOTH, FALSE);
            SubclassAutomationEdit(h);
            return h;
        };
        g_hEventEdit = createScanEdit(IDC_EVENTVWR_EDIT, true, true);
        g_hJarEdit = createScanEdit(IDC_JAR_EDIT);
        g_hExeEdit = createScanEdit(IDC_EXE_EDIT);
        g_hPfEdit = createScanEdit(IDC_PF_EDIT);
        g_hMcEdit = createScanEdit(IDC_MC_EDIT);

        g_hAutomationJarPage = CreateWindowExW(0, L"BUTTON", L"dll | jar", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_AUTOMATION_JAR_PAGE), g_hInst, nullptr);
        SendMessageW(g_hAutomationJarPage, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hAutomationJarPage);

        g_hJarActiv = CreateWindowExW(0, L"BUTTON", L"scan", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_JAR_ACTIV), g_hInst, nullptr);
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

        g_hPage4Nav = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PAGE4_NAV), g_hInst, nullptr);
        SendMessageW(g_hPage4Nav, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hPage4Nav);

        g_hModHash = CreateWindowExW(0, L"BUTTON", L"checkmods", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_MOD_HASH), g_hInst, nullptr);
        SendMessageW(g_hModHash, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hModHash);

        g_hModFullScan = CreateWindowExW(0, L"BUTTON", L"fullscan", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_MOD_FULLSCAN), g_hInst, nullptr);
        SendMessageW(g_hModFullScan, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hModFullScan);

        g_hDllUnsigned = CreateWindowExW(0, L"BUTTON", L"unsigned dll", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_DLL_UNSIGNED), g_hInst, nullptr);
        SendMessageW(g_hDllUnsigned, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hDllUnsigned);

        g_hDllActivity = CreateWindowExW(0, L"BUTTON", L".dll activ", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_DLL_ACTIVITY), g_hInst, nullptr);
        SendMessageW(g_hDllActivity, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hDllActivity);

        g_hPage5Nav = CreateWindowExW(0, L"BUTTON", L"injection", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PAGE5_NAV), g_hInst, nullptr);
        SendMessageW(g_hPage5Nav, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        MakeButtonNoErase(g_hPage5Nav);


        g_hInjGenCheck = CreateWindowExW(0, L"BUTTON", L"detect", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_INJGEN_CHECK), g_hInst, nullptr);
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
            if (!g_particlesPaused && !AnyAutomationScanWorking() && !IsIconic(hwnd))
                UpdateParticles(hwnd);

            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
    {
        POINT pt{};
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        int resizeTarget = 0;
        size_t resizeColumn = static_cast<size_t>(-1);
        float resizeWidth = 0.0f;
        if (AutomationColumnResizeHitTest(hwnd, pt, resizeTarget, resizeColumn, resizeWidth))
        {
            g_automationColumnResizing = true;
            g_automationColumnResizeTarget = resizeTarget;
            g_automationColumnResizeIndex = resizeColumn;
            g_automationColumnResizeStartX = pt.x;
            g_automationColumnResizeStartW = resizeWidth;
            SetCapture(hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }

        int jarSortColumn = AutomationJarActivityHeaderColumnAtPoint(hwnd, pt);
        if (jarSortColumn == 0 || jarSortColumn == 1)
        {
            SortAutomationTargetLinesByPipeColumnDateDesc(AUTOMATION_OUTPUT_JAR_ACTIVITY, static_cast<size_t>(jarSortColumn));
            SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_JAR_ACTIVITY);
            InvalidateAutomationPanel(AUTOMATION_OUTPUT_JAR_ACTIVITY);
            return 0;
        }

        int dllSortColumn = AutomationDllActivityHeaderColumnAtPoint(hwnd, pt);
        if (dllSortColumn == 0 || dllSortColumn == 1)
        {
            SortAutomationTargetLinesByPipeColumnDateDesc(AUTOMATION_OUTPUT_DLL_ACTIVITY, static_cast<size_t>(dllSortColumn));
            SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_DLL_ACTIVITY);
            InvalidateAutomationPanel(AUTOMATION_OUTPUT_DLL_ACTIVITY);
            return 0;
        }

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
        POINT pt{};
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        if (g_automationColumnResizing && g_automationColumnResizeTarget != 0)
        {
            float newW = std::max(36.0f, g_automationColumnResizeStartW + static_cast<float>(pt.x - g_automationColumnResizeStartX));
            std::vector<float>& overrides = g_automationColumnWidthOverrides[g_automationColumnResizeTarget];
            if (overrides.size() <= g_automationColumnResizeIndex)
                overrides.resize(g_automationColumnResizeIndex + 1, 0.0f);
            overrides[g_automationColumnResizeIndex] = newW;
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            InvalidateAutomationPanel(g_automationColumnResizeTarget);
            return 0;
        }

        int hoverTarget = 0;
        size_t hoverColumn = static_cast<size_t>(-1);
        float hoverWidth = 0.0f;
        if (AutomationColumnResizeHitTest(hwnd, pt, hoverTarget, hoverColumn, hoverWidth))
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));

        if (g_automationScrollDragging && g_automationScrollDragTarget != 0)
        {
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
        if (g_automationColumnResizing)
        {
            g_automationColumnResizing = false;
            g_automationColumnResizeTarget = 0;
            g_automationColumnResizeIndex = static_cast<size_t>(-1);
            g_automationColumnResizeStartX = 0;
            g_automationColumnResizeStartW = 0.0f;
            ReleaseCapture();
            return 0;
        }

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
        g_automationColumnResizing = false;
        g_automationColumnResizeTarget = 0;
        g_automationColumnResizeIndex = static_cast<size_t>(-1);
        g_automationColumnResizeStartX = 0;
        g_automationColumnResizeStartW = 0.0f;
        g_automationScrollDragging = false;
        g_automationScrollDragTarget = 0;
        break;
    }

    case WM_CONTEXTMENU:
    {
        POINT screenPt{};
        if (lParam == -1)
            GetCursorPos(&screenPt);
        else
        {
            screenPt.x = GET_X_LPARAM(lParam);
            screenPt.y = GET_Y_LPARAM(lParam);
        }

        POINT clientPt = screenPt;
        ScreenToClient(hwnd, &clientPt);

        std::wstring pathToCopy;
        std::wstring nameToCopy;
        int highlightTarget = 0;
        size_t highlightIndex = static_cast<size_t>(-1);
        if (AutomationPathAtPointForCopy(hwnd, clientPt, pathToCopy, &highlightTarget, &highlightIndex, &nameToCopy))
        {
            ShowAutomationCopyPathMenu(hwnd, screenPt, pathToCopy, highlightTarget, highlightIndex, nameToCopy);
            return 0;
        }
        break;
    }

    case WM_RBUTTONUP:
    {
        POINT clientPt{};
        clientPt.x = GET_X_LPARAM(lParam);
        clientPt.y = GET_Y_LPARAM(lParam);
        std::wstring pathToCopy;
        std::wstring nameToCopy;
        int highlightTarget = 0;
        size_t highlightIndex = static_cast<size_t>(-1);
        if (AutomationPathAtPointForCopy(hwnd, clientPt, pathToCopy, &highlightTarget, &highlightIndex, &nameToCopy))
        {
            POINT screenPt = clientPt;
            ClientToScreen(hwnd, &screenPt);
            ShowAutomationCopyPathMenu(hwnd, screenPt, pathToCopy, highlightTarget, highlightIndex, nameToCopy);
            return 0;
        }
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
            if (PointInRect(bamPanel, pt) && ScrollAutomationOutput(AUTOMATION_OUTPUT_PAGE4, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_PAGE4);
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_PAGE4);
                return 0;
            }

            if (PointInRect(eventPanel, pt) && ScrollAutomationOutput(AUTOMATION_OUTPUT_EVENTVWR, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                SyncAutomationEditScrollToState(AUTOMATION_OUTPUT_EVENTVWR);
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_EVENTVWR);
                return 0;
            }


            return 0;
        }

        if (g_currentPage == 2)
        {
            struct WheelPanel { RECT rc; int target; };
            WheelPanel panels[] = {
                { GetJarActivityPanelRect(rc), AUTOMATION_OUTPUT_JAR_ACTIVITY },
                { GetDllActivityPanelRect(rc), AUTOMATION_OUTPUT_DLL_ACTIVITY },
                { GetDllUnsignedPanelRect(rc), AUTOMATION_OUTPUT_DLL_UNSIGNED }
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
                { GetFullAutomationPanelRect(rc), AUTOMATION_OUTPUT_PAGE4 }
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
        HWND ctl = reinterpret_cast<HWND>(lParam);
        if (ctl == g_hEventEdit)
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            // Native EDIT controls cannot be reliably truly transparent: with
            // WS_EX_TRANSPARENT/HOLLOW_BRUSH the text may disappear and the
            // selection starts blinking. Use the same blended color as the
            // automation panel instead, so visually it stays seamless.
            SetTextColor(hdc, TEXT);
            SetBkColor(hdc, RGB(23, 24, 27));
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<LRESULT>(g_hAutomationEditBrush ? g_hAutomationEditBrush : GetStockObject(BLACK_BRUSH));
        }

        if (ctl == g_hJarEdit || ctl == g_hExeEdit || ctl == g_hPfEdit || ctl == g_hMcEdit)
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, TEXT);
            SetBkColor(hdc, RGB(23, 24, 27));
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
            ResizeMainWindowForPage(g_currentPage);
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_AUTOMATION_NAV)
        {
            g_currentPage = 1;
            ResizeMainWindowForPage(g_currentPage);
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_AUTOMATION_JAR_PAGE)
        {
            g_currentPage = 2;
            ResizeMainWindowForPage(g_currentPage);
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_PAGE4_NAV)
        {
            g_currentPage = 3;
            ResizeMainWindowForPage(g_currentPage);
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_PAGE5_NAV)
        {
            g_currentPage = 4;
            ResizeMainWindowForPage(g_currentPage);
            LayoutControls(hwnd);
            UpdateHistoryEditWindow();
            return 0;
        }

        if (id == IDC_AUTOMATION_BAM)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_EVENTVWR);
            return 0;
        }

        if (id == IDC_AUTOMATION_BAM_UNSIGNED_ONLY)
        {
            bool now = !g_bamUnsignedOnly.load();
            g_bamUnsignedOnly.store(now);
            InvalidateRect(g_hAutomationBamUnsignedOnly, nullptr, TRUE);
            if (g_currentPage == 1)
            {
                SetAutomationStatusLine(AUTOMATION_OUTPUT_BAM, now ? L"включён фильтр not signed only. Нажмите scan для сканирования." : L"фильтр not signed only выключен. Нажмите scan для сканирования.");
                InvalidateAutomationPanel(AUTOMATION_OUTPUT_BAM);
            }
            return 0;
        }

        if (id == IDC_AUTOMATION_EVENTVWR)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_EVENTVWR);
            return 0;
        }


        if (id == IDC_JAR_ACTIV)
        {
            g_activityScanMode.store(0);
            StartAutomationBatchScan({
                AUTOMATION_OUTPUT_JAR_ACTIVITY,
                AUTOMATION_OUTPUT_DLL_ACTIVITY,
                AUTOMATION_OUTPUT_DLL_UNSIGNED
            });
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
            StartAutomationBatchScan({ AUTOMATION_OUTPUT_PAGE4, AUTOMATION_OUTPUT_EVENTVWR });
            return 0;
        }

        if (id == IDC_MOD_FULLSCAN)
        {
            g_page4ScanMode.store(1);
            StartAutomationScan(AUTOMATION_OUTPUT_EVENTVWR);
            StartAutomationScan(AUTOMATION_OUTPUT_PAGE4);
            return 0;
        }

        if (id == IDC_DLL_UNSIGNED)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_DLL_UNSIGNED);
            return 0;
        }

        if (id == IDC_DLL_ACTIVITY)
        {
            StartAutomationScan(AUTOMATION_OUTPUT_DLL_ACTIVITY);
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
        InvalidateRect(g_hAutomationJarPage, nullptr, TRUE);
        InvalidateRect(g_hJarActiv, nullptr, TRUE);
        InvalidateRect(g_hExeDeleted, nullptr, TRUE);
        InvalidateRect(g_hPfDeleted, nullptr, TRUE);
        InvalidateRect(g_hMcLogs, nullptr, TRUE);
        InvalidateRect(g_hPage4Nav, nullptr, TRUE);
        InvalidateRect(g_hModHash, nullptr, TRUE);
        InvalidateRect(g_hModFullScan, nullptr, TRUE);
        InvalidateRect(g_hDllUnsigned, nullptr, TRUE);
        InvalidateRect(g_hDllActivity, nullptr, TRUE);
        InvalidateRect(g_hPage5Nav, nullptr, TRUE);
        InvalidateRect(g_hInjGenCheck, nullptr, TRUE);
        return 0;
    }

    case WM_APP_AUTOMATION_BATCH_DONE:
    {
        AutomationBatchResult* batch = reinterpret_cast<AutomationBatchResult*>(lParam);

        if (batch)
        {
            for (AutomationBatchItem& item : batch->items)
            {
                int finishedTarget = item.target;
                SetAutomationLines(finishedTarget, std::move(item.lines));
                if (AutomationEditForTarget(finishedTarget))
                    UpdateAutomationEditWindow(finishedTarget);
                AutomationWorkingRef(finishedTarget).store(false);
            }
            delete batch;
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        InvalidateRect(g_hAutomationBam, nullptr, TRUE);
        InvalidateRect(g_hAutomationBamUnsignedOnly, nullptr, TRUE);
        InvalidateRect(g_hAutomationEventVwr, nullptr, TRUE);
        InvalidateRect(g_hAutomationJarPage, nullptr, TRUE);
        InvalidateRect(g_hJarActiv, nullptr, TRUE);
        InvalidateRect(g_hExeDeleted, nullptr, TRUE);
        InvalidateRect(g_hPfDeleted, nullptr, TRUE);
        InvalidateRect(g_hMcLogs, nullptr, TRUE);
        InvalidateRect(g_hPage4Nav, nullptr, TRUE);
        InvalidateRect(g_hModHash, nullptr, TRUE);
        InvalidateRect(g_hModFullScan, nullptr, TRUE);
        InvalidateRect(g_hDllUnsigned, nullptr, TRUE);
        InvalidateRect(g_hDllActivity, nullptr, TRUE);
        InvalidateRect(g_hPage5Nav, nullptr, TRUE);
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


static void ResizeMainWindowForPage(int page)
{
    if (!g_hMain || IsZoomed(g_hMain))
        return;

    int wantedW = 1120;
    int wantedH = 760;
    if (page == 2)
    {
        wantedW = 1400;
        wantedH = 900;
    }
    else if (page == 1 || page == 4)
    {
        wantedW = 1240;
        wantedH = 860;
    }

    RECT wr{};
    GetWindowRect(g_hMain, &wr);
    int curW = wr.right - wr.left;
    int curH = wr.bottom - wr.top;
    if (curW == wantedW && curH == wantedH)
        return;

    SetWindowPos(g_hMain, nullptr, wr.left, wr.top, wantedW, wantedH, SWP_NOZORDER | SWP_NOACTIVATE);
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
