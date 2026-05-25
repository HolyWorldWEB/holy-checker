static void InitParticles(int width, int height)
{
    g_particles.clear();

    int safeW = std::max(width, 1);
    int safeH = std::max(height, 1);
    int safeParticleH = std::max(safeH - 70, 1);

    if (safeW < 100 || safeH < 100)
        return;

    srand(static_cast<unsigned int>(time(nullptr)));

    for (int i = 0; i < 40; ++i)
    {
        Particle p{};

        p.x = 2.0f + static_cast<float>(rand() % std::max(safeW - 4, 1));
        p.y = 2.0f + static_cast<float>(rand() % std::max(safeParticleH - 4, 1));

        float speedX = ((rand() % 200) - 100) / 420.0f;
        float speedY = ((rand() % 200) - 100) / 420.0f;

        if (speedX > -0.06f && speedX < 0.06f)
            speedX = 0.07f;

        if (speedY > -0.06f && speedY < 0.06f)
            speedY = -0.07f;

        p.vx = speedX;
        p.vy = speedY;
        p.size = 1.2f + static_cast<float>(rand() % 3);
        p.alpha = static_cast<BYTE>(26 + rand() % 38);

        g_particles.push_back(p);
    }
}

static void UpdateParticles(HWND hwnd)
{
    if (g_particlesPaused || IsIconic(hwnd) || !IsWindowVisible(hwnd))
        return;

    RECT rc{};
    GetClientRect(hwnd, &rc);

    int width = static_cast<int>(rc.right - rc.left);
    int height = static_cast<int>(rc.bottom - rc.top);

    if (width < 100 || height < 100)
        return;

    if (g_particles.empty())
        InitParticles(width, height);

    const float minX = 2.0f;
    const float minY = 2.0f;
    const float maxX = static_cast<float>(std::max(width - 6, 4));
    const float maxY = static_cast<float>(std::max(height - 70, 4));

    bool needReinit = false;

    for (const auto& p : g_particles)
    {
        if (p.x < -20.0f || p.x > maxX + 20.0f || p.y < -20.0f || p.y > maxY + 20.0f)
        {
            needReinit = true;
            break;
        }
    }

    if (needReinit)
    {
        InitParticles(width, height);
    }

    for (auto& p : g_particles)
    {
        p.x += p.vx;
        p.y += p.vy;

        if (p.x < minX) p.x = maxX;
        if (p.x > maxX) p.x = minX;
        if (p.y < minY) p.y = maxY;
        if (p.y > maxY) p.y = minY;
    }

    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN
    );
}

static void DrawParticles(Graphics& g, const RECT& rc)
{
    int width = static_cast<int>(rc.right - rc.left);
    int height = static_cast<int>(rc.bottom - rc.top);

    if (g_particlesPaused || width < 100 || height < 100 || g_particles.empty())
        return;

    int clipHeight = std::max(height - 58, 1);

    GraphicsState state = g.Save();
    g.SetClip(Rect(0, 0, static_cast<INT>(width), static_cast<INT>(clipHeight)), CombineModeReplace);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Pen linePen(Color(14, 120, 130, 160), 1.0f);

    for (size_t i = 0; i < g_particles.size(); ++i)
    {
        for (size_t j = i + 1; j < g_particles.size(); ++j)
        {
            float dx = g_particles[i].x - g_particles[j].x;
            float dy = g_particles[i].y - g_particles[j].y;
            float dist2 = dx * dx + dy * dy;

            if (dist2 < 3600.0f)
            {
                g.DrawLine(
                    &linePen,
                    g_particles[i].x,
                    g_particles[i].y,
                    g_particles[j].x,
                    g_particles[j].y
                );
            }
        }
    }

    for (const auto& p : g_particles)
    {
        if (p.x < 0.0f || p.y < 0.0f || p.x > width || p.y > clipHeight)
            continue;

        SolidBrush brush(Color(p.alpha, 120, 160, 255));
        g.FillEllipse(&brush, p.x, p.y, p.size, p.size);
    }

    g.Restore(state);
}

static void DrawAmbientBackground(Graphics& g, const RECT& rc)
{
    const float w = static_cast<float>(rc.right - rc.left);

    g.SetSmoothingMode(SmoothingModeAntiAlias);

    GraphicsPath blueGlow;
    blueGlow.AddEllipse(RectF(18.0f, 88.0f, 430.0f, 430.0f));
    PathGradientBrush blueBrush(&blueGlow);
    blueBrush.SetCenterColor(Color(44, 80, 135, 255));
    Color blueSurround[] = { Color(0, 80, 135, 255) };
    INT blueCount = 1;
    blueBrush.SetSurroundColors(blueSurround, &blueCount);
    g.FillPath(&blueBrush, &blueGlow);

    GraphicsPath whiteGlow;
    whiteGlow.AddEllipse(RectF(w - 360.0f, -160.0f, 420.0f, 420.0f));
    PathGradientBrush whiteBrush(&whiteGlow);
    whiteBrush.SetCenterColor(Color(24, 245, 248, 255));
    Color whiteSurround[] = { Color(0, 245, 248, 255) };
    INT whiteCount = 1;
    whiteBrush.SetSurroundColors(whiteSurround, &whiteCount);
    g.FillPath(&whiteBrush, &whiteGlow);
}

static void DrawLogo(Graphics& g)
{
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    SolidBrush blue(Color(245, 74, 134, 255));
    SolidBrush cutout(Color(255, 18, 18, 19));

    g.FillEllipse(&blue, 110.0f, 80.0f, 190.0f, 190.0f);
    g.FillEllipse(&cutout, 140.0f, 110.0f, 130.0f, 130.0f);

    PointF handle[] =
    {
        PointF(256.0f, 224.0f),
        PointF(322.0f, 290.0f),
        PointF(346.0f, 318.0f),
        PointF(337.0f, 332.0f),
        PointF(321.0f, 342.0f),
        PointF(292.0f, 320.0f),
        PointF(228.0f, 260.0f)
    };

    g.FillPolygon(&blue, handle, 7);
}

static void DrawTitle(Graphics& g, RECT rc)
{
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    FontFamily family(L"Segoe UI");
    Font titleFont(&family, 48.0f, FontStyleBold, UnitPixel);
    SolidBrush glowBrush(Color(95, 115, 165, 255));
    SolidBrush brush(Color(255, 246, 248, 255));

    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);

    RectF glowLayout(0.0f, 18.0f, static_cast<REAL>(rc.right), 62.0f);
    RectF layout(0.0f, 15.0f, static_cast<REAL>(rc.right), 62.0f);

    g.DrawString(L"HolyCheck", -1, &titleFont, glowLayout, &fmt, &glowBrush);
    g.DrawString(L"HolyCheck", -1, &titleFont, layout, &fmt, &brush);
}

static void DrawSectionSeparator(Graphics& g, RECT rc)
{
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Pen linePen(Color(120, 58, 58, 66), 1.0f);

    float right = static_cast<float>(rc.right - 24);

    if (right < 520.0f)
        right = 520.0f;

    g.DrawLine(&linePen, 520.0f, 166.0f, right, 166.0f);
}

static void DrawProgressBar(Graphics& g, RECT rc)
{
    const float marginX = static_cast<float>(PROGRESS_SIDE_MARGIN);
    const float barHeight = static_cast<float>(PROGRESS_BAR_H);
    const float cancelReserve = static_cast<float>(CANCEL_BUTTON_W + PROGRESS_CANCEL_GAP);

    float x = marginX;
    float y = static_cast<float>(rc.bottom - PROGRESS_BOTTOM_MARGIN - PROGRESS_BAR_H);
    float width = static_cast<float>(rc.right) - marginX * 2.0f - cancelReserve;

    if (width <= 10.0f)
        return;

    g.SetSmoothingMode(SmoothingModeAntiAlias);

    RectF bgRect(x, y, width, barHeight);

    GraphicsPath bgPath;
    bgPath.AddArc(bgRect.X, bgRect.Y, 12.0f, 12.0f, 180.0f, 90.0f);
    bgPath.AddArc(bgRect.GetRight() - 12.0f, bgRect.Y, 12.0f, 12.0f, 270.0f, 90.0f);
    bgPath.AddArc(bgRect.GetRight() - 12.0f, bgRect.GetBottom() - 12.0f, 12.0f, 12.0f, 0.0f, 90.0f);
    bgPath.AddArc(bgRect.X, bgRect.GetBottom() - 12.0f, 12.0f, 12.0f, 90.0f, 90.0f);
    bgPath.CloseFigure();

    SolidBrush bgBrush(Color(255, 42, 42, 46));
    g.FillPath(&bgBrush, &bgPath);

    float fillWidth = (width * static_cast<float>(g_progressPercent)) / 100.0f;

    if (fillWidth > 0.5f)
    {
        RectF fillRect(x, y, fillWidth, barHeight);

        GraphicsPath fillPath;

        if (fillWidth < 14.0f)
        {
            fillPath.AddRectangle(fillRect);
        }
        else
        {
            fillPath.AddArc(fillRect.X, fillRect.Y, 12.0f, 12.0f, 180.0f, 90.0f);
            fillPath.AddArc(fillRect.GetRight() - 12.0f, fillRect.Y, 12.0f, 12.0f, 270.0f, 90.0f);
            fillPath.AddArc(fillRect.GetRight() - 12.0f, fillRect.GetBottom() - 12.0f, 12.0f, 12.0f, 0.0f, 90.0f);
            fillPath.AddArc(fillRect.X, fillRect.GetBottom() - 12.0f, 12.0f, 12.0f, 90.0f, 90.0f);
            fillPath.CloseFigure();
        }

        SolidBrush fillBrush(Color(255, 128, 128, 140));
        g.FillPath(&fillBrush, &fillPath);
    }

    FontFamily family(L"Segoe UI");
    Font font(&family, 11.0f, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 235, 235, 240));

    std::wstring percentText = std::to_wstring(g_progressPercent) + L"%";

    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);

    RectF textRect(x, y - 1.0f, width, barHeight + 2.0f);
    g.DrawString(percentText.c_str(), -1, &font, textRect, &fmt, &textBrush);
}

static void DrawDownloadLog(Graphics& g, RECT rc)
{
    RECT panelRect = GetDownloadLogRect(rc);

    if (rc.right < panelRect.right + 24)
        return;

    std::vector<std::wstring> lines;
    int scrollOffset = 0;

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        lines = g_logLines;
        ClampLogScrollOffsetLocked();
        scrollOffset = g_logScrollOffset;
    }

    if (lines.empty())
        return;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RectF bgRect(
        static_cast<REAL>(panelRect.left),
        static_cast<REAL>(panelRect.top),
        static_cast<REAL>(panelRect.right - panelRect.left),
        static_cast<REAL>(panelRect.bottom - panelRect.top)
    );

    GraphicsPath bgPath;
    bgPath.AddArc(bgRect.X, bgRect.Y, 14.0f, 14.0f, 180.0f, 90.0f);
    bgPath.AddArc(bgRect.GetRight() - 14.0f, bgRect.Y, 14.0f, 14.0f, 270.0f, 90.0f);
    bgPath.AddArc(bgRect.GetRight() - 14.0f, bgRect.GetBottom() - 14.0f, 14.0f, 14.0f, 0.0f, 90.0f);
    bgPath.AddArc(bgRect.X, bgRect.GetBottom() - 14.0f, 14.0f, 14.0f, 90.0f, 90.0f);
    bgPath.CloseFigure();

    SolidBrush bgBrush(Color(125, 28, 30, 36));
    Pen borderPen(Color(132, 122, 128, 142), 1.0f);

    g.FillPath(&bgBrush, &bgPath);
    g.DrawPath(&borderPen, &bgPath);

    FontFamily family(L"Segoe UI");
    Font titleFont(&family, 12.0f, FontStyleBold, UnitPixel);
    Font lineFont(&family, 11.0f, FontStyleRegular, UnitPixel);
    SolidBrush titleBrush(Color(245, 236, 238, 245));
    SolidBrush textBrush(Color(255, 235, 235, 240));
    SolidBrush hintBrush(Color(210, 160, 160, 168));

    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);

    const float x = static_cast<float>(panelRect.left);
    const float y = static_cast<float>(panelRect.top);
    const float panelW = static_cast<float>(panelRect.right - panelRect.left);
    const float panelH = static_cast<float>(panelRect.bottom - panelRect.top);

    RectF titleRect(x + 10.0f, y + 7.0f, panelW - 20.0f, 16.0f);
    g.DrawString(L"Лог загрузки", -1, &titleFont, titleRect, &fmt, &titleBrush);

    float firstLineY = y + 28.0f;

    if (lines.size() > LOG_VISIBLE_LINES)
    {
        StringFormat rightFmt;
        rightFmt.SetAlignment(StringAlignmentFar);
        rightFmt.SetTrimming(StringTrimmingEllipsisCharacter);
        rightFmt.SetFormatFlags(StringFormatFlagsNoWrap);

        RectF hintRect(x + 10.0f, y + 7.0f, panelW - 20.0f, 16.0f);
        g.DrawString(L"колесо мыши", -1, &lineFont, hintRect, &rightFmt, &hintBrush);
    }

    size_t visibleLines = std::min(LOG_VISIBLE_LINES, lines.size());
    size_t endIndex = lines.size();

    if (scrollOffset > 0)
    {
        size_t offset = static_cast<size_t>(scrollOffset);
        endIndex = offset < lines.size() ? lines.size() - offset : visibleLines;
    }

    if (endIndex > lines.size())
        endIndex = lines.size();

    size_t startIndex = endIndex > visibleLines ? endIndex - visibleLines : 0;

    float lineY = firstLineY;
    const float lineH = 13.0f;

    for (size_t i = startIndex; i < endIndex; ++i)
    {
        RectF textRect(x + 10.0f, lineY, panelW - 20.0f, lineH + 2.0f);
        g.DrawString(lines[i].c_str(), -1, &lineFont, textRect, &fmt, &textBrush);
        lineY += lineH;

        if (lineY > y + panelH - 8.0f)
            break;
    }
}

static std::wstring PreviewText(const wchar_t* text, size_t maxLen)
{
    std::wstring s = text ? text : L"";

    for (wchar_t& ch : s)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            ch = L' ';
    }

    if (s.size() > maxLen)
        s = s.substr(0, maxLen - 3) + L"...";

    return s;
}

static void DrawHelperTextPanel(Graphics& g, RECT rc)
{
    RECT panelRect = GetHelperTextRect(rc);

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RectF bgRect(
        static_cast<REAL>(panelRect.left),
        static_cast<REAL>(panelRect.top),
        static_cast<REAL>(panelRect.right - panelRect.left),
        static_cast<REAL>(panelRect.bottom - panelRect.top)
    );

    GraphicsPath bgPath;
    bgPath.AddArc(bgRect.X, bgRect.Y, 16.0f, 16.0f, 180.0f, 90.0f);
    bgPath.AddArc(bgRect.GetRight() - 16.0f, bgRect.Y, 16.0f, 16.0f, 270.0f, 90.0f);
    bgPath.AddArc(bgRect.GetRight() - 16.0f, bgRect.GetBottom() - 16.0f, 16.0f, 16.0f, 0.0f, 90.0f);
    bgPath.AddArc(bgRect.X, bgRect.GetBottom() - 16.0f, 16.0f, 16.0f, 90.0f, 90.0f);
    bgPath.CloseFigure();

    SolidBrush bgBrush(Color(92, 30, 34, 45));
    Pen borderPen(Color(92, 120, 124, 136), 1.0f);
    g.FillPath(&bgBrush, &bgPath);
    g.DrawPath(&borderPen, &bgPath);

    FontFamily family(L"Segoe UI");
    Font titleFont(&family, 12.0f, FontStyleBold, UnitPixel);
    Font rowFont(&family, 10.0f, FontStyleRegular, UnitPixel);
    SolidBrush titleBrush(Color(245, 238, 240, 246));
    SolidBrush textBrush(Color(228, 210, 214, 224));
    SolidBrush labelBrush(Color(235, 174, 188, 218));

    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);

    const float x = static_cast<float>(panelRect.left);
    const float y = static_cast<float>(panelRect.top);
    const float w = static_cast<float>(panelRect.right - panelRect.left);

    g.DrawString(L"Копирование", -1, &titleFont, RectF(x + 10.0f, y + 8.0f, w - 20.0f, 16.0f), &fmt, &titleBrush);

    struct Row { const wchar_t* label; const wchar_t* value; } rows[] =
    {
        { L"Читы", EVERYTHING_QUERY_1 },
        { L"Конфиги", EVERYTHING_QUERY_2 },
        { L"Check", L"Проверка системы, служб, журналов и списка твинков" },
        { L"Сайты", POWERSHELL_SITES_TXT }
    };

    float rowY = y + 30.0f;
    for (const auto& row : rows)
    {
        g.DrawString(row.label, -1, &rowFont, RectF(x + 10.0f, rowY, 70.0f, 16.0f), &fmt, &labelBrush);
        std::wstring preview = PreviewText(row.value, 54);
        g.DrawString(preview.c_str(), -1, &rowFont, RectF(x + 82.0f, rowY, w - 178.0f, 16.0f), &fmt, &textBrush);
        rowY += 27.0f;
    }
}

static void DrawScene(HWND hwnd, HDC targetHdc)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    if (w <= 0 || h <= 0)
        return;

    HDC memDC = CreateCompatibleDC(targetHdc);
    HBITMAP memBmp = CreateCompatibleBitmap(targetHdc, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    HBRUSH bgBrush = CreateSolidBrush(BG);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    DrawAmbientBackground(g, rc);
    DrawParticles(g, rc);
    DrawLogo(g);
    DrawSectionSeparator(g, rc);
    DrawDownloadLog(g, rc);
    DrawHelperTextPanel(g, rc);
    DrawProgressBar(g, rc);

    BitBlt(targetHdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

static void MoveChildWindow(HWND child, int x, int y, int w, int h, BOOL repaint)
{
    if (!child)
        return;

    RECT current{};
    GetWindowRect(child, &current);

    HWND parent = GetParent(child);
    POINT pts[2] =
    {
        { current.left, current.top },
        { current.right, current.bottom }
    };

    MapWindowPoints(nullptr, parent, pts, 2);

    if (pts[0].x == x && pts[0].y == y &&
        pts[1].x - pts[0].x == w && pts[1].y - pts[0].y == h)
    {
        return;
    }

    MoveWindow(child, x, y, w, h, repaint);
}

static void SetChildVisible(HWND child, bool visible)
{
    if (!child)
        return;

    bool current = IsWindowVisible(child) != FALSE;

    if (current == visible)
        return;

    ShowWindow(child, visible ? SW_SHOWNA : SW_HIDE);
}

static void LayoutControls(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);

    int width = rc.right;

    MoveChildWindow(g_hDeleteHolyCheck, 28, 22, 118, 34, TRUE);
    ShowWindow(g_hDeleteHolyCheck, SW_SHOWNA);

    const int topButtonW = 126;
    const int topButtonH = 34;
    const int topGap = 9;
    const int topY = 16;
    int powerX = width - 28 - topButtonW;
    int servicesX = powerX - topGap - topButtonW;

    MoveChildWindow(g_hEnableServices, servicesX, topY, topButtonW, topButtonH, TRUE);
    MoveChildWindow(g_hPowerShell, powerX, topY, topButtonW, topButtonH, TRUE);
    ShowWindow(g_hEnableServices, SW_SHOWNA);
    ShowWindow(g_hPowerShell, SW_SHOWNA);

    int cancelX = width - PROGRESS_SIDE_MARGIN - CANCEL_BUTTON_W;
    int progressY = rc.bottom - PROGRESS_BOTTOM_MARGIN - PROGRESS_BAR_H;
    int cancelY = progressY + (PROGRESS_BAR_H - CANCEL_BUTTON_H) / 2;
    MoveChildWindow(g_hCancelDownload, cancelX, cancelY, CANCEL_BUTTON_W, CANCEL_BUTTON_H, TRUE);
    ShowWindow(g_hCancelDownload, SW_SHOWNA);

    RECT helperRect = GetHelperTextRect(rc);
    int copyX = helperRect.right - COPY_BUTTON_W - 14;
    int copyY = helperRect.top + 33;
    MoveChildWindow(g_hCopyEverything1, copyX, copyY, COPY_BUTTON_W, COPY_BUTTON_H, TRUE);
    MoveChildWindow(g_hCopyEverything2, copyX, copyY + 28, COPY_BUTTON_W, COPY_BUTTON_H, TRUE);
    MoveChildWindow(g_hCopyPowerShell, copyX, copyY + 56, COPY_BUTTON_W, COPY_BUTTON_H, TRUE);
    MoveChildWindow(g_hCopySites, copyX, copyY + 84, COPY_BUTTON_W, COPY_BUTTON_H, TRUE);
    ShowWindow(g_hCopyEverything1, SW_SHOWNA);
    ShowWindow(g_hCopyEverything2, SW_SHOWNA);
    ShowWindow(g_hCopyPowerShell, SW_SHOWNA);
    ShowWindow(g_hCopySites, SW_SHOWNA);

    int rightX = 520;
    int tabsY = 74;

    int tabW = 124;
    int tabH = 30;
    int gap = 7;

    for (int i = 0; i < static_cast<int>(g_tabButtons.size()); ++i)
    {
        MoveChildWindow(
            g_tabButtons[i],
            rightX + i * (tabW + gap),
            tabsY,
            tabW,
            tabH,
            TRUE
        );
        ShowWindow(g_tabButtons[i], SW_SHOWNA);
    }

    MoveChildWindow(g_hDownloadAll, rightX, tabsY + 44, 148, 34, TRUE);
    ShowWindow(g_hDownloadAll, SW_SHOWNA);

    int tileW = 138;
    int tileH = 40;
    int gapX = 12;
    int gapY = 12;
    int tileStartY = tabsY + 104;

    int availableW = std::max(width - rightX - 28, tileW);
    int columns = std::max(3, (availableW + gapX) / (tileW + gapX));
    if (columns > 5)
        columns = 5;

    for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
    {
        if (g_items[i].tab != g_currentTab)
            ShowWindow(g_tileButtons[i], SW_HIDE);
    }

    RECT clearRect{ rightX - 18, tabsY + 88, rc.right, rc.bottom - LOG_INVALIDATE_HEIGHT + 6 };
    RedrawWindow(
        hwnd,
        &clearRect,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN | RDW_UPDATENOW
    );

    int visible = 0;

    for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
    {
        if (g_items[i].tab != g_currentTab)
            continue;

        HWND btn = g_tileButtons[i];
        int col = visible % columns;
        int row = visible / columns;

        MoveChildWindow(
            btn,
            rightX + col * (tileW + gapX),
            tileStartY + row * (tileH + gapY),
            tileW,
            tileH,
            TRUE
        );

        ShowWindow(btn, SW_SHOWNA);
        InvalidateRect(btn, nullptr, TRUE);
        ++visible;
    }

    for (HWND b : g_tabButtons)
        InvalidateRect(b, nullptr, TRUE);

    for (HWND b : g_tileButtons)
    {
        if (IsWindowVisible(b))
            InvalidateRect(b, nullptr, TRUE);
    }

    InvalidateRect(g_hDownloadAll, nullptr, TRUE);
    InvalidateRect(g_hCancelDownload, nullptr, TRUE);
    InvalidateRect(g_hDeleteHolyCheck, nullptr, TRUE);
    InvalidateRect(g_hEnableServices, nullptr, TRUE);
    InvalidateRect(g_hPowerShell, nullptr, TRUE);
    InvalidateRect(g_hCopyEverything1, nullptr, TRUE);
    InvalidateRect(g_hCopyEverything2, nullptr, TRUE);
    InvalidateRect(g_hCopyPowerShell, nullptr, TRUE);
    InvalidateRect(g_hCopySites, nullptr, TRUE);

    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW
    );
}

static void PaintParentSceneForButton(HWND button, HDC hdc)
{
    UNREFERENCED_PARAMETER(button);

    RECT rc{};
    GetClientRect(button, &rc);

    HBRUSH brush = CreateSolidBrush(BG);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
}

static void DrawRoundButton(DRAWITEMSTRUCT* d)
{
    HDC hdc = d->hDC;
    RECT r = d->rcItem;

    int id = static_cast<int>(d->CtlID);

    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    bool disabled = ((d->itemState & ODS_DISABLED) != 0) || !IsWindowEnabled(d->hwndItem);
    bool selected = false;

    if (id >= IDC_TAB_BASE && id < IDC_TAB_BASE + TAB_COUNT)
        selected = (id - IDC_TAB_BASE == g_currentTab);

    bool copyButton = id == IDC_COPY_EVERYTHING_1 ||
        id == IDC_COPY_EVERYTHING_2 ||
        id == IDC_COPY_POWERSHELL ||
        id == IDC_COPY_SITES;

    COLORREF fill = PANEL;
    COLORREF borderColor = BORDER;
    COLORREF textColor = TEXT;

    if (disabled)
    {
        fill = RGB(48, 48, 52);
        borderColor = RGB(62, 62, 68);
        textColor = RGB(145, 145, 152);
    }
    else if (selected)
    {
        fill = RGB(58, 58, 66);
        borderColor = RGB(135, 145, 160);
    }
    else if (pressed)
    {
        fill = PANEL_PRESSED;
        borderColor = RGB(105, 112, 126);
    }
    else
    {
        fill = PANEL;
        borderColor = BORDER;
    }

    if (!disabled && (id == IDC_POWERSHELL || id == IDC_ENABLE_SERVICES || id == IDC_DELETE_HOLYCHECK))
    {
        fill = pressed ? RGB(70, 72, 82) : RGB(52, 54, 62);
        borderColor = RGB(92, 98, 112);
    }

    if (!disabled && copyButton)
    {
        fill = pressed ? RGB(54, 56, 64) : RGB(42, 44, 50);
        borderColor = RGB(78, 84, 96);
    }

    PaintParentSceneForButton(d->hwndItem, hdc);

    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldBrush = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    int radius = copyButton ? 10 : 14;
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(br);

    wchar_t text[256]{};
    GetWindowTextW(d->hwndItem, text, 256);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);

    HFONT buttonFont = g_hFont;
    if (copyButton)
        buttonFont = g_hTinyFont ? g_hTinyFont : g_hSmallFont;

    HGDIOBJ oldFont = SelectObject(hdc, buttonFont);

    DrawTextW(
        hdc,
        text,
        -1,
        &r,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK ButtonNoEraseProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(subclassId);
    UNREFERENCED_PARAMETER(refData);

    if (msg == WM_ERASEBKGND)
        return 1;

    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, ButtonNoEraseProc, 1);

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void MakeButtonNoErase(HWND button)
{
    if (button)
    {
        LONG_PTR exStyle = GetWindowLongPtrW(button, GWL_EXSTYLE);
        SetWindowLongPtrW(button, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
        SetWindowSubclass(button, ButtonNoEraseProc, 1, 0);
    }
}

static std::wstring BuildMinecraftPowerShellCommand()
{
    std::wstring script;
    script.reserve(36000);

    script += LR"HCPS($ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
function Out($s){[Console]::WriteLine([string]$s)}
try{
    Remove-Module PSReadLine -Force -ErrorAction SilentlyContinue
    Out 'Команда выполнена: Remove-Module PSReadLine -Force'
}catch{
    Out ('Команда не выполнена: '+$_.Exception.Message)
}
Out ''

$excluded=@('labymod','labymod-neo')
$skip=@(
    'node_modules','.git','.svn','.hg','__pycache__','.gradle','.m2',
    'temp','tmp','cache','caches','$recycle.bin','system volume information',
    'packages','package cache','npm-cache','pip','pip-cache'
)

function N($p){
    if([string]::IsNullOrWhiteSpace($p)){return ''}
    try{return ([System.IO.Path]::GetFullPath([string]$p) -replace '[\\/]+$','')}catch{return [string]$p}
}
function JoinP([string[]]$parts){
    if($null -eq $parts -or $parts.Count -eq 0){return ''}
    $r=[string]$parts[0]
    for($i=1;$i -lt $parts.Count;$i++){$r=[System.IO.Path]::Combine($r,[string]$parts[$i])}
    return $r
}
function Leaf($p){try{return [System.IO.Path]::GetFileName((N $p))}catch{return ''}}
function Parent($p){try{return [System.IO.Directory]::GetParent((N $p)).FullName}catch{return ''}}

$script:userRoot=N $env:USERPROFILE
$script:userExact=$script:userRoot.ToLowerInvariant()
$script:userKey=$script:userExact
if($script:userKey.Length -gt 0){if($script:userKey[$script:userKey.Length-1] -ne '\'){$script:userKey+='\'}}

function InUser($p){
    $full=(N $p).ToLowerInvariant()
    if($script:userKey.Length -eq 0){return $false}
    return (($full -eq $script:userExact) -or $full.StartsWith($script:userKey))
}
function IsExcluded($p){
    $l=(Leaf $p)
    if([string]::IsNullOrWhiteSpace($l)){return $false}
    return ($excluded -contains $l.ToLowerInvariant())
}
function IsReparsePoint($p){
    try{
        $di=[System.IO.DirectoryInfo]::new($p)
        return (($di.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
    }catch{return $true}
}
function SkipDir($p){
    $l=(Leaf $p)
    if([string]::IsNullOrWhiteSpace($l)){return $false}
    $ll=$l.ToLowerInvariant()
    if(($excluded -contains $ll) -or ($skip -contains $ll)){return $true}
    return (IsReparsePoint $p)
}
)HCPS";

    script += LR"HCPS(function HasJar($versions){
    if(-not [System.IO.Directory]::Exists($versions)){return $false}
    try{
        foreach($f in [System.IO.Directory]::EnumerateFiles($versions,'*.jar',[System.IO.SearchOption]::AllDirectories)){
            return $true
        }
    }catch{}
    return $false
}
function IsMcRoot($p,$allowOutsideUser){
    $full=N $p
    if([string]::IsNullOrWhiteSpace($full)){return $false}
    if((-not $allowOutsideUser) -and (-not (InUser $full))){return $false}
    if(IsExcluded $full){return $false}
    if(-not [System.IO.Directory]::Exists($full)){return $false}

    $assets=[System.IO.Path]::Combine($full,'assets')
    $versions=[System.IO.Path]::Combine($full,'versions')
    $logs=[System.IO.Path]::Combine($full,'logs')

    if(-not [System.IO.Directory]::Exists($assets)){return $false}
    if(-not [System.IO.Directory]::Exists($versions)){return $false}
    if(-not [System.IO.Directory]::Exists($logs)){return $false}
    return (HasJar $versions)
}

function ResetFound(){
    $script:valid=[System.Collections.Generic.List[string]]::new()
    $script:validSeen=@{}
}
function AddValid($p,$allowOutsideUser){
    $full=N $p
    if([string]::IsNullOrWhiteSpace($full)){return}
    if((-not $allowOutsideUser) -and (-not (InUser $full))){return}
    if(-not (IsMcRoot $full $allowOutsideUser)){return}
    $key=$full.ToLowerInvariant()
    if($script:validSeen.ContainsKey($key)){return}
    $script:validSeen[$key]=$true
    [void]$script:valid.Add($full)
}
function TryCandidateAndParents($p,$allowOutsideUser){
    $full=N $p
    if([string]::IsNullOrWhiteSpace($full)){return}
    AddValid $full $allowOutsideUser
    $leaf=(Leaf $full).ToLowerInvariant()
    if($leaf -eq 'assets' -or $leaf -eq 'logs' -or $leaf -eq 'versions'){
        $par=Parent $full
        if(-not [string]::IsNullOrWhiteSpace($par)){AddValid $par $allowOutsideUser}
    }
}

function GetFastPaths(){
    $a=N $env:APPDATA
    $list=[System.Collections.Generic.List[string]]::new()
    if(-not [string]::IsNullOrWhiteSpace($a)){
        [void]$list.Add((JoinP @($a,'.tlauncher','legacy','Minecraft','game')))
        [void]$list.Add((JoinP @($a,'.minecraft')))
    }
    [void]$list.Add('C:\PulseVisuals\game')
    return $list.ToArray()
}
function GetPriorityRoots(){
    $list=[System.Collections.Generic.List[string]]::new()
    function A($x){$n=N $x;if($n -and (InUser $n) -and [System.IO.Directory]::Exists($n)){[void]$list.Add($n)}}
    A $env:APPDATA
    A (JoinP @($env:APPDATA,'.minecraft'))
    A (JoinP @($env:APPDATA,'.tlauncher'))
    A (JoinP @($env:USERPROFILE,'Desktop'))
    A (JoinP @($env:USERPROFILE,'Downloads'))
    A (JoinP @($env:USERPROFILE,'Documents'))
    A (JoinP @($env:USERPROFILE,'Games'))
    A (JoinP @($env:USERPROFILE,'OneDrive'))
    A $env:LOCALAPPDATA
    A $env:USERPROFILE
    return $list.ToArray()
}
)HCPS";

    script += LR"HCPS(function AddExistingFastPath($p){
    $full=N $p
    if([string]::IsNullOrWhiteSpace($full)){return}
    if(IsExcluded $full){return}
    if(-not [System.IO.Directory]::Exists($full)){return}
    $key=$full.ToLowerInvariant()
    if($script:validSeen.ContainsKey($key)){return}
    $script:validSeen[$key]=$true
    [void]$script:valid.Add($full)
}
function RunFast(){
    Out 'Быстрый режим.'
    foreach($p in (GetFastPaths)){AddExistingFastPath $p}
}
function RunFull(){
    Out 'Полный режим.'
    if([string]::IsNullOrWhiteSpace($script:userRoot) -or -not [System.IO.Directory]::Exists($script:userRoot)){
        Out 'Не удалось определить папку пользователя.'
        return
    }

    $scanLimit=800000
    $scanned=0
    $seenDirs=@{}
    $queue=[System.Collections.Generic.Queue[string]]::new()

    foreach($root in (GetPriorityRoots)){
        $r=N $root
        if(-not [string]::IsNullOrWhiteSpace($r)){
            $rk=$r.ToLowerInvariant()
            if(-not $seenDirs.ContainsKey($rk)){$queue.Enqueue($r)}
        }
    }

    while($queue.Count -gt 0 -and $scanned -lt $scanLimit){
        $d=N ([string]$queue.Dequeue())
        if([string]::IsNullOrWhiteSpace($d)){continue}
        if(-not (InUser $d)){continue}

        $dk=$d.ToLowerInvariant()
        if($seenDirs.ContainsKey($dk)){continue}
        $seenDirs[$dk]=$true

        if(SkipDir $d){continue}
        $scanned++
        TryCandidateAndParents $d $false

        try{$children=[System.IO.Directory]::EnumerateDirectories($d)}catch{continue}
        foreach($child in $children){
            $c=N $child
            if([string]::IsNullOrWhiteSpace($c)){continue}
            if(-not (InUser $c)){continue}
            if(SkipDir $c){continue}
            TryCandidateAndParents $c $false
            $ck=$c.ToLowerInvariant()
            if(-not $seenDirs.ContainsKey($ck)){$queue.Enqueue($c)}
        }
    }

    if($scanned -ge $scanLimit){Out ('Лимит поиска достигнут: '+$scanLimit+' папок. Показаны найденные пути.')}
}
)HCPS";

    script += LR"HCPS(function ReadLongLine([string]$prompt){
    if($null -ne $prompt){[Console]::Write($prompt)}
    $sb=[System.Text.StringBuilder]::new()
    while($true){
        $k=[Console]::ReadKey($true)
        if($k.Key -eq [System.ConsoleKey]::Enter){
            [Console]::WriteLine()
            return $sb.ToString()
        }
        if($k.Key -eq [System.ConsoleKey]::Backspace){
            if($sb.Length -gt 0){
                [void]$sb.Remove($sb.Length-1,1)
                try{[Console]::Write([char]8);[Console]::Write(' ');[Console]::Write([char]8)}catch{}
            }
            continue
        }
        if(($k.Modifiers -band [System.ConsoleModifiers]::Control) -and $k.Key -eq [System.ConsoleKey]::C){
            [Console]::WriteLine()
            return 'exit'
        }
        if(($k.Modifiers -band [System.ConsoleModifiers]::Control) -and $k.Key -eq [System.ConsoleKey]::V){
            try{
                Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue
                $clip=[System.Windows.Forms.Clipboard]::GetText()
                if($null -ne $clip){
                    $clip=$clip -replace "`r",'' -replace "`n",''
                    [void]$sb.Append($clip)
                    [Console]::Write($clip)
                }
            }catch{}
            continue
        }
        $ch=$k.KeyChar
        if([int][char]$ch -eq 0){continue}
        [void]$sb.Append($ch)
        [Console]::Write($ch)
    }
}
function AskMode(){
    ClearScreen
    Out 'Главное меню:'
    Out '----------------------------------------'
    Out ' 1) Быстрый'
    Out ' 2) Полный'
    Out '----------------------------------------'
    while($true){
        $raw=ReadLongLine 'Введите 1, 2 или exit: '
        if($null -eq $raw){return 0}
        $t=$raw.Trim().ToLowerInvariant()
        if($t -eq 'exit'){ClearScreen; Out 'Главное меню:'; Out '----------------------------------------'; Out ' 1) Быстрый'; Out ' 2) Полный'; Out '----------------------------------------'; continue}
        $n=0
        if([int]::TryParse($t,[ref]$n) -and ($n -eq 1 -or $n -eq 2)){return $n}
        Out 'Неверный ввод. Повторите выбор.'
    }
}
function SortedFound(){
    [string[]]$items=$script:valid.ToArray()
    [System.Array]::Sort($items,[System.StringComparer]::OrdinalIgnoreCase)
    foreach($item in $items){$item}
}
function ClearScreen(){
    try{[Console]::Write(([char]27).ToString()+'[2J'+([char]27).ToString()+'[3J'+([char]27).ToString()+'[H')}catch{}
    try{[Console]::Clear()}catch{}
}
function GoHome(){
    ClearScreen
    return 'menu'
}
function ChooseFolder(){
    Out ''
    Out 'Найденные папки Minecraft:'
    if($script:valid.Count -eq 0){
        Out 'Подходящие папки не найдены.'
        Out 'Введите exit для возврата в главное меню.'
        while($true){
            $raw=ReadLongLine '> '
            if($null -eq $raw){return 'menu'}
            if($raw.Trim().ToLowerInvariant() -eq 'exit'){return (GoHome)}
        }
    }

    [string[]]$items=@(SortedFound)
    Out ('Найдено: '+$items.Count)
    Out 'Выберите папку:'
    Out '----------------------------------------'
    for($i=0;$i -lt $items.Count;$i++){Out (('{0,2}) {1}' -f ($i+1),$items[$i]))}
    Out '----------------------------------------'
    while($true){
        $raw=ReadLongLine ('Введите номер 1-'+$items.Count+' или exit: ')
        if($null -eq $raw){return 'menu'}
        $t=$raw.Trim().ToLowerInvariant()
        if($t -eq 'exit'){return (GoHome)}
        $idx=0
        if([int]::TryParse($t,[ref]$idx) -and $idx -ge 1 -and $idx -le $items.Count){
            $chosen=$items[$idx-1]
            try{$ExecutionContext.SessionState.Path.SetLocation($chosen)}catch{try{[System.IO.Directory]::SetCurrentDirectory($chosen)}catch{}}
            Out ('Выбран путь: '+$chosen)
            Out 'Чтобы вернуться в главное меню, напишите exit.'
            return 'shell'
        }
        Out 'Неверный ввод. Повторите выбор.'
    }
}
)HCPS";

    script += LR"HCPS(function CommandLoop(){
    while($true){
        try{$loc=[string]$ExecutionContext.SessionState.Path.CurrentLocation}catch{$loc=[System.IO.Directory]::GetCurrentDirectory()}
        $cmd=ReadLongLine ('PS '+$loc+'> ')
        if($null -eq $cmd){return}
        if($cmd.Trim().ToLowerInvariant() -eq 'exit'){ClearScreen;return}
        if([string]::IsNullOrWhiteSpace($cmd)){continue}
        try{
            $result=[scriptblock]::Create($cmd).Invoke()
            if($null -ne $result){foreach($r in $result){if($null -ne $r){Out $r}}}
        }catch{
            Out ('Ошибка: '+$_.Exception.Message)
        }
    }
}

while($true){
    ResetFound
    $mode=AskMode
    if($mode -eq 0){break}
    Out ''
    if($mode -eq 1){RunFast}else{RunFull}
    $next=ChooseFolder
    if($next -eq 'shell'){CommandLoop}
    Out ''
}
)HCPS";

    return L"-NoLogo -NoExit -EncodedCommand " + Base64EncodePowerShellCommand(script);
}

static void OpenPowerShellMinecraft()
{
    std::wstring command = BuildMinecraftPowerShellCommand();
    std::wstring profile = GetCurrentUserProfilePath();
    const wchar_t* workingDir = DirectoryExists(profile) ? profile.c_str() : nullptr;

    ShellExecuteW(
        g_hMain,
        L"runas",
        L"powershell.exe",
        command.c_str(),
        workingDir,
        SW_SHOWNORMAL
    );
}

static void OpenCmdEnableServices()
{
    std::wstring command =
        L"/k sc config PcaSVC start= auto & sc start PcaSVC & "
        L"sc config DPS start= auto & sc start DPS & "
        L"sc config SysMain start= auto & sc start SysMain & "
        L"sc config EventLog start= auto & sc start EventLog & "
        L"sc config bam start= auto & sc start bam";

    ShellExecuteW(
        g_hMain,
        L"runas",
        L"cmd.exe",
        command.c_str(),
        nullptr,
        SW_SHOWNORMAL
    );
}

