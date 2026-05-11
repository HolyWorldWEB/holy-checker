// 08_ui_rendering_layout.inc
// Particles, GDI+ drawing, layout, owner-drawn buttons, helper commands.
// Original lines: 4627-5526

static void InitParticles(int width, int height)
{
    g_particles.clear();

    int safeW = std::max(width, 1);
    int safeH = std::max(height, 1);
    int safeParticleH = std::max(safeH - 70, 1);

    if (safeW < 100 || safeH < 100)
        return;

    srand(static_cast<unsigned int>(time(nullptr)));

    for (int i = 0; i < 90; ++i)
    {
        Particle p{};

        p.x = 2.0f + static_cast<float>(rand() % std::max(safeW - 4, 1));
        p.y = 2.0f + static_cast<float>(rand() % std::max(safeParticleH - 4, 1));

        float speedX = ((rand() % 200) - 100) / 190.0f;
        float speedY = ((rand() % 200) - 100) / 190.0f;

        if (speedX > -0.06f && speedX < 0.06f)
            speedX = 0.16f;

        if (speedY > -0.06f && speedY < 0.06f)
            speedY = -0.16f;

        p.vx = speedX;
        p.vy = speedY;
        p.size = 1.2f + static_cast<float>(rand() % 3);
        p.alpha = static_cast<BYTE>(42 + rand() % 70);

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

    // Старый тёмный фон без сетки, но с мягким бело-синим светом.
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

    // Простая лупа: без внешней прозрачной обводки, но с нормальным прорезом.
    g.FillEllipse(&blue, 106.0f, 78.0f, 207.0f, 207.0f);
    g.FillEllipse(&cutout, 139.0f, 111.0f, 141.0f, 141.0f);

    // Ручка чуть уже, без лишних линий.
    PointF handle[] =
    {
        PointF(268.0f, 235.0f),
        PointF(333.0f, 300.0f),
        PointF(358.0f, 330.0f),
        PointF(349.0f, 344.0f),
        PointF(332.0f, 354.0f),
        PointF(303.0f, 332.0f),
        PointF(238.0f, 271.0f)
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
    const float marginX = 22.0f;
    const float barHeight = 16.0f;

    RECT logRectNative = GetDownloadLogRect(rc);
    const float y = static_cast<float>(logRectNative.top) +
        (static_cast<float>(LOG_PANEL_H) - barHeight) / 2.0f;

    const float logGap = 18.0f;
    float x = static_cast<float>(LOG_PANEL_X + LOG_PANEL_W) + logGap;
    const float cancelReserve = static_cast<float>(CANCEL_BUTTON_W) + 18.0f;
    float width = static_cast<float>(rc.right) - x - marginX - cancelReserve;

    if (width < 220.0f)
    {
        x = marginX;
        width = static_cast<float>(rc.right) - marginX * 2.0f - cancelReserve;
    }

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

    std::wstring activeText = GetActiveDownloadsText();

    if (lines.empty() && activeText.empty())
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

    if (!activeText.empty())
    {
        SolidBrush activeBrush(Color(255, 245, 245, 248));
        RectF activeRect(x + 10.0f, y + 25.0f, panelW - 20.0f, 16.0f);
        g.DrawString(activeText.c_str(), -1, &lineFont, activeRect, &fmt, &activeBrush);
        firstLineY = y + 45.0f;
    }

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
    DrawProgressBar(g, rc);
    DrawDownloadLog(g, rc);
    DrawHelperTextPanel(g, rc);

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

    RECT logRect = GetDownloadLogRect(rc);
    int cancelX = width - 28 - CANCEL_BUTTON_W;
    int cancelY = logRect.top + (LOG_PANEL_H - CANCEL_BUTTON_H) / 2;
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

    // Сначала скрываем все плитки неактивных вкладок. Это важно: иначе при
    // быстрой смене вкладок Windows может оставить на фоне старый owner-draw
    // рисунок кнопки, и кажется, что программы перемешались между вкладками.
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

    // Раньше каждая owner-draw кнопка заново рисовала всю сцену окна через
    // DrawScene(). При старте и переключении вкладок это давало заметное
    // мигание и задержку появления кнопок. Для кнопок достаточно быстро
    // очистить локальный фон тем же базовым цветом.
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
        // Не ставим WS_EX_TRANSPARENT: из-за него owner-draw кнопки иногда
        // появляются только после клика/перерисовки. Прозрачность делаем
        // вручную цветами в DrawRoundButton.
        LONG_PTR exStyle = GetWindowLongPtrW(button, GWL_EXSTYLE);
        SetWindowLongPtrW(button, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
        SetWindowSubclass(button, ButtonNoEraseProc, 1, 0);
    }
}

static void OpenPowerShellMinecraft()
{
    std::wstring minecraftFolder = GetMinecraftFolder();
    DeleteFileW(CombinePath(minecraftFolder, L"powershell.txt").c_str());

    std::wstring escapedFolder = EscapePowerShellSingleQuoted(minecraftFolder);
    std::wstring command =
        L"-NoExit -Command \"Set-Location -LiteralPath '" + escapedFolder + L"'\"";

    ShellExecuteW(
        g_hMain,
        L"runas",
        L"powershell.exe",
        command.c_str(),
        minecraftFolder.c_str(),
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

