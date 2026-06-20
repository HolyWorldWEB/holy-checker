// Used by drawing code before 09_window_main.inc is included.
static bool IsPathLikeAutomationCell(const std::wstring& cell);

static void InitParticles(int width, int height)
{
    g_particles.clear();

    int safeW = std::max(width, 1);
    int safeH = std::max(height, 1);
    int safeParticleH = std::max(safeH - 70, 1);

    if (safeW < 100 || safeH < 100)
        return;

    srand(static_cast<unsigned int>(time(nullptr)));

    for (int i = 0; i < 28; ++i)
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

    if (g_particlesPaused || AnyAutomationScanWorking() || width < 100 || height < 100 || g_particles.empty())
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
        { std::wstring displayLine = StripAutomationDeletedMarker(lines[i]); g.DrawString(displayLine.c_str(), -1, &lineFont, textRect, &fmt, &textBrush); }
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


static std::vector<std::wstring> SplitAutomationPipeColumns(const std::wstring& line)
{
    std::wstring displayLine = StripAutomationDeletedMarker(line);
    std::vector<std::wstring> parts;
    size_t start = 0;

    while (start <= displayLine.size())
    {
        size_t pos = displayLine.find(L" | ", start);

        if (pos == std::wstring::npos)
        {
            parts.push_back(displayLine.substr(start));
            break;
        }

        parts.push_back(displayLine.substr(start, pos - start));
        start = pos + 3;
    }

    return parts;
}

static bool IsBamDataRowForColumns(const std::wstring& line)
{
    if (line.size() < 5 || !iswdigit(line[0]))
        return false;

    std::vector<std::wstring> cols = SplitAutomationPipeColumns(line);
    return cols.size() == 4;
}

static void DrawBamDataRow(Graphics& g, const std::wstring& line, const Font& lineFont, float x, float y, float panelW, float lineH)
{
    std::vector<std::wstring> cols = SplitAutomationPipeColumns(line);

    if (cols.size() != 4)
        return;

    SolidBrush textBrush(Color(255, 235, 235, 240));
    SolidBrush deletedNameBrush(Color(255, 255, 76, 76));
    SolidBrush forbiddenNameBrush(Color(255, 255, 76, 76));
    SolidBrush archiveNameBrush(Color(255, 255, 214, 82));
    SolidBrush sepBrush(Color(180, 150, 150, 158));
    SolidBrush sigBrush(Color(255, 255, 75, 75));

    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);

    const float left = x + 10.0f;
    const float usableW = panelW - 30.0f; // keep clear of the blue scrollbar
    const float sepW = 6.0f;
    float dateW = 126.0f;
    float programW = 158.0f;
    float sigW = 82.0f;

    if (usableW >= 900.0f)
    {
        dateW = 132.0f;
        programW = 176.0f;
        sigW = 88.0f;
    }
    else if (usableW >= 720.0f)
    {
        dateW = 128.0f;
        programW = 162.0f;
        sigW = 84.0f;
    }
    else if (usableW < 520.0f)
    {
        dateW = 112.0f;
        programW = 92.0f;
        sigW = 62.0f;
    }

    float pathW = usableW - dateW - programW - sigW - sepW * 3.0f;

    if (pathW < 72.0f)
    {
        float need = 72.0f - pathW;
        float reduceProgram = std::min(need, std::max(0.0f, programW - 62.0f));
        programW -= reduceProgram;
        need -= reduceProgram;

        float reduceDate = std::min(need, std::max(0.0f, dateW - 96.0f));
        dateW -= reduceDate;
        need -= reduceDate;

        float reduceSig = std::min(need, std::max(0.0f, sigW - 52.0f));
        sigW -= reduceSig;

        pathW = usableW - dateW - programW - sigW - sepW * 3.0f;
    }

    if (pathW < 50.0f)
        pathW = 50.0f;

    float cur = left;
    g.DrawString(cols[0].c_str(), -1, &lineFont, RectF(cur, y, dateW, lineH + 2.0f), &fmt, &textBrush);
    cur += dateW;
    g.DrawString(L"|", -1, &lineFont, RectF(cur, y, sepW, lineH + 2.0f), &fmt, &sepBrush);
    cur += sepW;

    g.DrawString(cols[1].c_str(), -1, &lineFont, RectF(cur, y, programW, lineH + 2.0f), &fmt, &textBrush);
    cur += programW;
    g.DrawString(L"|", -1, &lineFont, RectF(cur, y, sepW, lineH + 2.0f), &fmt, &sepBrush);
    cur += sepW;

    g.DrawString(cols[2].c_str(), -1, &lineFont, RectF(cur, y, pathW, lineH + 2.0f), &fmt, &textBrush);
    cur += pathW;
    g.DrawString(L"|", -1, &lineFont, RectF(cur, y, sepW, lineH + 2.0f), &fmt, &sepBrush);
    cur += sepW;

    g.DrawString(cols[3].c_str(), -1, &lineFont, RectF(cur, y, sigW, lineH + 2.0f), &fmt, &sigBrush);
}


static bool IsPipeSeparatedAutomationRow(const std::wstring& line)
{
    std::vector<std::wstring> cols = SplitAutomationPipeColumns(line);
    if (cols.size() < 2)
        return false;

    if (line == L"не найдено" || line.find(L"не удалось") != std::wstring::npos)
        return false;

    return true;
}

static size_t AutomationPathColumnForDraw(int target, const std::vector<std::wstring>& cols)
{
    // Current .jar activity / .exe activity format is fixed:
    // date | program | path.  Do not auto-detect the program column as a path
    // just because the program name ends with .jar/.exe; otherwise the real
    // path column becomes narrow and the full path is hidden.
    if (target == AUTOMATION_OUTPUT_EXE_DELETED && cols.size() >= 3)
        return 2;

    if (target == AUTOMATION_OUTPUT_BAM && cols.size() >= 3)
        return 2;
    for (size_t i = cols.size(); i-- > 0; )
    {
        if (IsPathLikeAutomationCell(cols[i]))
            return i;
    }
    return static_cast<size_t>(-1);
}

static std::vector<std::wstring> NormalizeAutomationColumnsForDraw(const std::wstring& line, int target)
{
    std::vector<std::wstring> cols = SplitAutomationPipeColumns(line);

    if ((target == AUTOMATION_OUTPUT_EXE_DELETED || target == AUTOMATION_OUTPUT_MC_LOGS || target == AUTOMATION_OUTPUT_PF_DELETED) && cols.size() > 3)
    {
        std::wstring action;
        for (size_t i = 1; i + 1 < cols.size(); ++i)
        {
            if (!action.empty())
                action += L", ";
            action += cols[i];
        }

        std::vector<std::wstring> fixedCols;
        fixedCols.push_back(cols[0]);
        fixedCols.push_back(action);
        fixedCols.push_back(cols.back());
        cols.swap(fixedCols);
    }

    return cols;
}

static std::vector<float> ComputeAutomationColumnWidths(Graphics& g, const std::vector<std::wstring>& lines, size_t startIndex, size_t endIndex, int target, const Font& lineFont, float panelW)
{
    const float usableW = std::max(60.0f, panelW - 32.0f);
    const float sepW = 6.0f;

    StringFormat measureFmt;
    measureFmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    auto measureCell = [&](const std::wstring& text) -> float {
        RectF bounds;
        g.MeasureString(text.c_str(), -1, &lineFont, PointF(0.0f, 0.0f), &measureFmt, &bounds);
        return std::ceil(bounds.Width) + 16.0f;
    };

    size_t maxCols = 0;
    size_t pathCol = static_cast<size_t>(-1);
    for (size_t i = startIndex; i < endIndex; ++i)
    {
        std::vector<std::wstring> cols = NormalizeAutomationColumnsForDraw(lines[i], target);
        if (cols.size() < 2)
            continue;
        maxCols = std::max(maxCols, cols.size());
        size_t pc = AutomationPathColumnForDraw(target, cols);
        if (pc != static_cast<size_t>(-1))
            pathCol = pc;
    }

    if (maxCols == 0)
        return {};

    std::vector<float> widths(maxCols, 54.0f);
    for (size_t i = startIndex; i < endIndex; ++i)
    {
        std::vector<std::wstring> cols = NormalizeAutomationColumnsForDraw(lines[i], target);
        for (size_t c = 0; c < cols.size(); ++c)
            widths[c] = std::max(widths[c], measureCell(cols[c]));
    }

    if (pathCol == static_cast<size_t>(-1) || pathCol >= widths.size())
    {
        for (size_t c = widths.size(); c-- > 0; )
        {
            // Prefer the rightmost path-like column when the target has no fixed path column.
            for (size_t i = startIndex; i < endIndex; ++i)
            {
                std::vector<std::wstring> cols = NormalizeAutomationColumnsForDraw(lines[i], target);
                if (c < cols.size() && IsPathLikeAutomationCell(cols[c]))
                {
                    pathCol = c;
                    break;
                }
            }
            if (pathCol != static_cast<size_t>(-1))
                break;
        }
    }

    // Keep small metadata columns compact and give the path/name column all remaining room.
    if (widths.size() >= 3)
    {
        for (size_t c = 0; c < widths.size(); ++c)
        {
            if (c == pathCol)
                continue;
            float cap = (c == 0 ? 170.0f : 210.0f);
            if ((target == AUTOMATION_OUTPUT_JAR_ACTIVITY || target == AUTOMATION_OUTPUT_DLL_ACTIVITY) && c == 1)
                cap = 620.0f; // действие: created/deleted/rename + USN date in brackets
            if (target == AUTOMATION_OUTPUT_INJGEN && c == 1)
                cap = 72.0f; // процесс: javaw.exe, держим колонку короткой
            widths[c] = std::min(widths[c], cap);
            if ((target == AUTOMATION_OUTPUT_JAR_ACTIVITY || target == AUTOMATION_OUTPUT_DLL_ACTIVITY) && c == 1)
                widths[c] = std::max(widths[c], 430.0f);
            if (target == AUTOMATION_OUTPUT_INJGEN && c == 1)
                widths[c] = std::min(widths[c], 72.0f);
        }
    }

    float separators = sepW * static_cast<float>(widths.size() > 0 ? widths.size() - 1 : 0);
    float total = separators;
    for (float w : widths)
        total += w;

    if (pathCol != static_cast<size_t>(-1) && pathCol < widths.size())
    {
        float fixed = separators;
        for (size_t c = 0; c < widths.size(); ++c)
            if (c != pathCol)
                fixed += widths[c];

        const float minPathW = std::min(260.0f, std::max(150.0f, usableW * 0.45f));
        if (fixed + minPathW > usableW)
        {
            float fixedOnly = std::max(1.0f, fixed - separators);
            float fixedBudget = std::max(80.0f, usableW - minPathW - separators);
            float scale = std::max(0.35f, fixedBudget / fixedOnly);
            for (size_t c = 0; c < widths.size(); ++c)
            {
                if (c != pathCol)
                    widths[c] = std::max(38.0f, widths[c] * scale);
            }
            fixed = separators;
            for (size_t c = 0; c < widths.size(); ++c)
                if (c != pathCol)
                    fixed += widths[c];
        }

        // The path/name column is the anchor column: it is measured by the longest
        // path in the whole result set, then it receives any free space. That keeps
        // all rows under the same vertical columns and prevents per-screen jumps.
        widths[pathCol] = std::max(widths[pathCol], std::max(140.0f, usableW - fixed));

        total = separators;
        for (float w : widths)
            total += w;
    }

    if (total > usableW)
    {
        if (pathCol != static_cast<size_t>(-1) && pathCol < widths.size())
        {
            float fixed = separators;
            for (size_t c = 0; c < widths.size(); ++c)
                if (c != pathCol)
                    fixed += widths[c];
            widths[pathCol] = std::max(90.0f, usableW - fixed);
        }
        else
        {
            float scale = (usableW - separators) / std::max(1.0f, total - separators);
            for (float& w : widths)
                w = std::max(48.0f, w * scale);
        }
    }

    return widths;
}


static bool IsAutomationResizableColumnTarget(int target)
{
    return target == AUTOMATION_OUTPUT_JAR_ACTIVITY ||
        target == AUTOMATION_OUTPUT_DLL_ACTIVITY ||
        target == AUTOMATION_OUTPUT_DLL_UNSIGNED ||
        target == AUTOMATION_OUTPUT_PAGE4;
}

static void ApplyAutomationColumnWidthOverrides(int target, std::vector<float>& widths)
{
    auto it = g_automationColumnWidthOverrides.find(target);
    if (it == g_automationColumnWidthOverrides.end())
        return;

    if (it->second.size() < widths.size())
        it->second.resize(widths.size(), 0.0f);

    for (size_t i = 0; i < widths.size() && i < it->second.size(); ++i)
    {
        if (it->second[i] > 0.0f)
            widths[i] = std::max(36.0f, it->second[i]);
    }
}

static std::vector<int> GetAutomationResizeTargetsForCurrentPage()
{
    if (g_currentPage == 1)
        return { AUTOMATION_OUTPUT_PAGE4 };
    if (g_currentPage == 2)
        return { AUTOMATION_OUTPUT_JAR_ACTIVITY, AUTOMATION_OUTPUT_DLL_ACTIVITY, AUTOMATION_OUTPUT_DLL_UNSIGNED };
    return {};
}

static bool AutomationColumnResizeHitTest(HWND hwnd, POINT pt, int& outTarget, size_t& outColumn, float& outColumnWidth)
{
    outTarget = 0;
    outColumn = static_cast<size_t>(-1);
    outColumnWidth = 0.0f;

    std::vector<int> targets = GetAutomationResizeTargetsForCurrentPage();
    if (targets.empty())
        return false;

    RECT rc{};
    GetClientRect(hwnd, &rc);

    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return false;
    Graphics g(hdc);
    FontFamily family(L"Consolas");
    Font lineFont(&family, 13.0f, FontStyleRegular, UnitPixel);

    bool hit = false;
    for (int target : targets)
    {
        if (!IsAutomationResizableColumnTarget(target))
            continue;

        RECT panel = GetAutomationPanelRectForTarget(target, rc);
        if (!PointInRect(panel, pt))
            continue;
        if (pt.y < panel.top + 28 || pt.y > panel.bottom - 8)
            continue;

        std::vector<std::wstring> lines;
        int scrollOffset = 0;
        GetAutomationSnapshot(target, lines, scrollOffset);
        if (lines.empty())
            continue;

        std::vector<float> widths = ComputeAutomationColumnWidths(g, lines, 0, lines.size(), target, lineFont, static_cast<float>(panel.right - panel.left));
        ApplyAutomationColumnWidthOverrides(target, widths);
        if (widths.size() < 2)
            continue;

        const float left = static_cast<float>(panel.left) + 10.0f;
        const float sepW = 6.0f;
        float cur = left;
        for (size_t i = 0; i + 1 < widths.size(); ++i)
        {
            cur += widths[i];
            if (((static_cast<float>(pt.x) - cur) >= -5.0f && (static_cast<float>(pt.x) - cur) <= 5.0f))
            {
                outTarget = target;
                outColumn = i;
                outColumnWidth = widths[i];
                hit = true;
                break;
            }
            cur += sepW;
        }
        if (hit)
            break;
    }

    ReleaseDC(hwnd, hdc);
    return hit;
}

static void DrawAutomationPipeSeparatedRow(Graphics& g, const std::wstring& line, int target, const Font& lineFont, float x, float y, float panelW, float lineH, size_t rowIndex = static_cast<size_t>(-1), const std::vector<float>* sharedWidths = nullptr)
{
    std::vector<std::wstring> cols = NormalizeAutomationColumnsForDraw(line, target);
    if (cols.size() < 2)
        return;

    SolidBrush textBrush(Color(255, 235, 235, 240));
    SolidBrush deletedNameBrush(Color(255, 255, 76, 76));
    SolidBrush forbiddenNameBrush(Color(255, 255, 76, 76));
    SolidBrush archiveNameBrush(Color(255, 255, 214, 82));
    SolidBrush sepBrush(Color(180, 150, 150, 158));
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsLineLimit);

    const float left = x + 10.0f;
    const float sepW = 6.0f;
    float cur = left;

    size_t pathCol = AutomationPathColumnForDraw(target, cols);
    std::vector<float> widths;
    if (sharedWidths && sharedWidths->size() >= cols.size())
        widths = *sharedWidths;
    else
        widths = ComputeAutomationColumnWidths(g, std::vector<std::wstring>{ line }, 0, 1, target, lineFont, panelW);

    bool highlightRow = (target == g_automationCopyHighlightTarget && rowIndex == g_automationCopyHighlightIndex);
    SolidBrush highlightBrush(Color(38, 92, 155, 255));
    Pen highlightPen(Color(230, 125, 170, 255), 1.0f);

    if (highlightRow)
    {
        float rowW = std::max(20.0f, panelW - 24.0f);
        g.FillRectangle(&highlightBrush, RectF(left - 4.0f, y - 1.0f, rowW, lineH + 1.0f));
        g.DrawRectangle(&highlightPen, RectF(left - 4.0f, y - 1.0f, rowW, lineH + 1.0f));
    }

    for (size_t i = 0; i < cols.size(); ++i)
    {
        float colW = (i < widths.size()) ? widths[i] : 60.0f;

        bool deletedRow = AutomationLineHasDeletedMarker(line);
        bool archiveRow = AutomationLineHasArchiveMarker(line);
        bool forbiddenRow = AutomationLineHasForbiddenMarker(line);
        Brush* cellBrush = (deletedRow && i > 0) ? static_cast<Brush*>(&deletedNameBrush) : static_cast<Brush*>(&textBrush);
        if (forbiddenRow && target == AUTOMATION_OUTPUT_PAGE4 && i == 0)
            cellBrush = static_cast<Brush*>(&forbiddenNameBrush);
        else if (archiveRow && i == 0)
            cellBrush = static_cast<Brush*>(&archiveNameBrush);
        g.DrawString(cols[i].c_str(), -1, &lineFont, RectF(cur, y, colW, lineH + 2.0f), &fmt, cellBrush);
        cur += colW;

        if (i + 1 < cols.size())
        {
            g.DrawString(L"|", -1, &lineFont, RectF(cur, y, sepW, lineH + 2.0f), &fmt, &sepBrush);
            cur += sepW;
        }
    }
}

static void DrawAutomationOutputPanel(Graphics& g, RECT panelRect, const wchar_t* title, int target)
{
    std::vector<std::wstring> lines;
    int scrollOffset = 0;
    GetAutomationSnapshot(target, lines, scrollOffset);

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

    bool transparentPanel = (target == AUTOMATION_OUTPUT_JAR_ACTIVITY || target == AUTOMATION_OUTPUT_JAR_MODIFIED || target == AUTOMATION_OUTPUT_EXE_DELETED || target == AUTOMATION_OUTPUT_MC_LOGS || target == AUTOMATION_OUTPUT_PF_DELETED || target == AUTOMATION_OUTPUT_DLL_UNSIGNED || target == AUTOMATION_OUTPUT_DLL_ACTIVITY);
    SolidBrush bgBrush(transparentPanel ? Color(18, 28, 30, 36) : Color(125, 28, 30, 36));
    Pen borderPen(Color(132, 122, 128, 142), 1.0f);

    if (!transparentPanel)
        g.FillPath(&bgBrush, &bgPath);
    g.DrawPath(&borderPen, &bgPath);

    FontFamily family(L"Consolas");
    Font titleFont(&family, 13.0f, FontStyleRegular, UnitPixel);
    Font lineFont(&family, 13.0f, FontStyleRegular, UnitPixel);
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

    g.DrawString(title, -1, &titleFont, RectF(x + 10.0f, y + 6.0f, panelW - 20.0f, 20.0f), &fmt, &titleBrush);

    if (target == AUTOMATION_OUTPUT_EVENTVWR)
        return;

    const float lineH = 15.0f;
    size_t panelLineCapacityHint = static_cast<size_t>(std::max(1.0f, (panelRect.bottom - panelRect.top - 34.0f) / lineH));

    if (lines.size() > panelLineCapacityHint)
    {
        StringFormat rightFmt;
        rightFmt.SetAlignment(StringAlignmentFar);
        rightFmt.SetTrimming(StringTrimmingEllipsisCharacter);
        rightFmt.SetFormatFlags(StringFormatFlagsNoWrap);
        g.DrawString(L"scroll", -1, &lineFont, RectF(x + 10.0f, y + 7.0f, panelW - 20.0f, 16.0f), &rightFmt, &hintBrush);
    }

    if (!lines.empty() && lines.size() > panelLineCapacityHint)
    {
        int maxOffsetBar = std::max<int>(1, static_cast<int>(lines.size()) - static_cast<int>(panelLineCapacityHint));
        float trackX = x + panelW - 10.0f;
        float trackY = y + 30.0f;
        float trackH = panelH - 39.0f;
        SolidBrush trackBrush(Color(90, 45, 58, 86));
        SolidBrush thumbBrush(Color(240, 82, 155, 255));
        g.FillRectangle(&trackBrush, RectF(trackX, trackY, 6.0f, trackH));
        float thumbH = std::max(22.0f, trackH * (static_cast<float>(panelLineCapacityHint) / static_cast<float>(lines.size())));
        float thumbY = trackY + (trackH - thumbH) * (static_cast<float>(scrollOffset) / static_cast<float>(maxOffsetBar));
        g.FillRectangle(&thumbBrush, RectF(trackX - 2.0f, thumbY, 10.0f, thumbH));
    }

    if (lines.empty())
    {
        g.DrawString(L"Нажмите кнопку слева.", -1, &lineFont, RectF(x + 10.0f, y + 31.0f, panelW - 20.0f, 20.0f), &fmt, &hintBrush);
        return;
    }

    size_t visibleLines = std::min(panelLineCapacityHint, lines.size());
    size_t startIndex = static_cast<size_t>(std::max(0, scrollOffset));
    if (startIndex >= lines.size())
        startIndex = lines.size() > visibleLines ? lines.size() - visibleLines : 0;
    size_t endIndex = std::min(lines.size(), startIndex + visibleLines);

    float lineY = y + 30.0f;
    SolidBrush rowBrush(Color(26, 92, 116, 170));
    Pen rowLinePen(Color(84, 105, 118, 142), 1.0f);
    // Calculate columns from the full result set, not only from currently visible
    // rows. The longest path/name becomes the base for every row, so scrolling or
    // new rows do not make columns shift independently.
    std::vector<float> sharedPipeWidths = ComputeAutomationColumnWidths(g, lines, 0, lines.size(), target, lineFont, panelW);
    ApplyAutomationColumnWidthOverrides(target, sharedPipeWidths);

    for (size_t i = startIndex; i < endIndex; ++i)
    {
        RectF textRect(x + 10.0f, lineY, panelW - 30.0f, lineH + 1.0f);
        bool pipeRow = IsPipeSeparatedAutomationRow(lines[i]);
        bool bamRow = target == AUTOMATION_OUTPUT_BAM && IsBamDataRowForColumns(lines[i]);
        bool linedRow = bamRow || (target != AUTOMATION_OUTPUT_EVENTVWR && pipeRow);

        if (linedRow)
        {
            if ((i - startIndex) % 2 == 0)
                g.FillRectangle(&rowBrush, RectF(x + 7.0f, lineY - 1.0f, panelW - 25.0f, lineH + 1.0f));
            g.DrawLine(&rowLinePen, x + 8.0f, lineY + lineH, x + panelW - 17.0f, lineY + lineH);
        }

        if (bamRow)
            DrawBamDataRow(g, lines[i], lineFont, x, lineY, panelW, lineH);
        else if (target != AUTOMATION_OUTPUT_EVENTVWR && pipeRow)
            DrawAutomationPipeSeparatedRow(g, lines[i], target, lineFont, x, lineY, panelW, lineH, i, &sharedPipeWidths);
        else
            { std::wstring displayLine = StripAutomationDeletedMarker(lines[i]); g.DrawString(displayLine.c_str(), -1, &lineFont, textRect, &fmt, &textBrush); }

        lineY += lineH;

        if (lineY > y + panelH - 8.0f)
            break;
    }
}


struct AutomationServiceUiRow
{
    const wchar_t* label;
    const wchar_t* serviceName;
    bool running;
};

static bool QueryAutomationServiceRunning(const wchar_t* serviceName, bool& running)
{
    running = false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);

    if (!scm)
        return false;

    SC_HANDLE service = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);

    if (!service)
    {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    BOOL ok = QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &bytesNeeded
    );

    if (ok)
        running = status.dwCurrentState == SERVICE_RUNNING;

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return ok == TRUE;
}

static std::vector<AutomationServiceUiRow> GetAutomationServiceRows()
{
    static ULONGLONG lastTick = 0;
    static std::vector<AutomationServiceUiRow> cached;

    ULONGLONG now = GetTickCount64();

    if (!cached.empty() && now - lastTick < 1200)
        return cached;

    struct ServiceDef
    {
        const wchar_t* label;
        const wchar_t* name;
    };

    static const ServiceDef defs[] =
    {
        { L"bam", L"bam" },
        { L"pcascv", L"PcaSvc" },
        { L"sysmain", L"SysMain" },
        { L"dps", L"DPS" },
        { L"eventlog", L"EventLog" }
    };

    std::vector<AutomationServiceUiRow> rows;
    rows.reserve(sizeof(defs) / sizeof(defs[0]));

    for (const ServiceDef& def : defs)
    {
        bool running = false;
        QueryAutomationServiceRunning(def.name, running);
        rows.push_back({ def.label, def.name, running });
    }

    cached = rows;
    lastTick = now;
    return cached;
}

static void DrawAutomationServicesPanel(Graphics& g, RECT panelRect)
{
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

    FontFamily family(L"Consolas");
    Font titleFont(&family, 13.0f, FontStyleRegular, UnitPixel);
    Font lineFont(&family, 13.0f, FontStyleRegular, UnitPixel);

    SolidBrush titleBrush(Color(245, 236, 238, 245));
    SolidBrush nameBrush(Color(255, 235, 235, 240));
    SolidBrush onBrush(Color(255, 70, 220, 105));
    SolidBrush offBrush(Color(255, 255, 75, 75));

    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);

    StringFormat rightFmt;
    rightFmt.SetAlignment(StringAlignmentFar);
    rightFmt.SetTrimming(StringTrimmingEllipsisCharacter);
    rightFmt.SetFormatFlags(StringFormatFlagsNoWrap);

    const float x = static_cast<float>(panelRect.left);
    const float y = static_cast<float>(panelRect.top);
    const float w = static_cast<float>(panelRect.right - panelRect.left);

    g.DrawString(L"Службы", -1, &titleFont, RectF(x + 12.0f, y + 8.0f, w - 24.0f, 18.0f), &fmt, &titleBrush);

    std::vector<AutomationServiceUiRow> rows = GetAutomationServiceRows();
    float rowY = y + 30.0f;
    const float rowH = 16.0f;

    for (const AutomationServiceUiRow& row : rows)
    {
        g.DrawString(row.label, -1, &lineFont, RectF(x + 14.0f, rowY, w - 92.0f, rowH), &fmt, &nameBrush);
        const wchar_t* status = row.running ? L"вкл" : L"выкл";
        Brush* statusBrush = row.running ? static_cast<Brush*>(&onBrush) : static_cast<Brush*>(&offBrush);
        g.DrawString(status, -1, &lineFont, RectF(x + 12.0f, rowY, w - 24.0f, rowH), &rightFmt, statusBrush);
        rowY += rowH;
    }
}

static void DrawAutomationScreen(Graphics& g, RECT rc)
{
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RECT checkModsPanel = GetAutomationBamPanelRect(rc);
    RECT eventPanel = GetAutomationEventPanelRect(rc);
    RECT servicesPanel = GetAutomationServicesPanelRect(rc);

    DrawAutomationOutputPanel(g, checkModsPanel, L"checkmods", AUTOMATION_OUTPUT_PAGE4);
    DrawAutomationOutputPanel(g, eventPanel, L"eventvwr", AUTOMATION_OUTPUT_EVENTVWR);
    DrawAutomationServicesPanel(g, servicesPanel);
}

static void DrawJarActivityScreen(Graphics& g, RECT rc)
{
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    DrawAutomationOutputPanel(g, GetJarActivityPanelRect(rc), L".jar activity", AUTOMATION_OUTPUT_JAR_ACTIVITY);
    DrawAutomationOutputPanel(g, GetDllActivityPanelRect(rc), L".dll activity", AUTOMATION_OUTPUT_DLL_ACTIVITY);
    DrawAutomationOutputPanel(g, GetDllUnsignedPanelRect(rc), L"unsigned dll", AUTOMATION_OUTPUT_DLL_UNSIGNED);
}

static void DrawPage4Screen(Graphics& g, RECT rc)
{
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    RECT panel = GetFullAutomationPanelRect(rc);
    RectF textRect(static_cast<REAL>(panel.left + 14), static_cast<REAL>(panel.top + 14),
        static_cast<REAL>(panel.right - panel.left - 28), 80.0f);
    FontFamily family(L"Segoe UI");
    Font font(&family, 14.0f, FontStyleBold, UnitPixel);
    SolidBrush brush(Color(230, 236, 238, 245));
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(L"checkmods перенесён во вкладку check, unsigned dll перенесён во вкладку dll | jar.", -1, &font, textRect, &fmt, &brush);
}

static void DrawPage5Screen(Graphics& g, RECT rc)
{
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    DrawAutomationOutputPanel(g, GetPage5InjGenPanelRect(rc), L"injection", AUTOMATION_OUTPUT_INJGEN);
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

    if (g_currentPage == 0)
        DrawLogo(g);

    DrawSectionSeparator(g, rc);

    if (g_currentPage == 1)
    {
        DrawAutomationScreen(g, rc);
    }
    else if (g_currentPage == 2)
    {
        DrawJarActivityScreen(g, rc);
    }
    else if (g_currentPage == 3)
    {
        DrawPage4Screen(g, rc);
    }
    else if (g_currentPage == 4)
    {
        DrawPage5Screen(g, rc);
    }
    else
    {
        DrawDownloadLog(g, rc);
        DrawHelperTextPanel(g, rc);
        DrawProgressBar(g, rc);
    }

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

    const int topButtonH = 30;
    const int topGap = 6;
    const int topY = 7;
    const int navButtonW = 116;
    const int checkButtonW = navButtonW;
    int navX = width - 24 - navButtonW;

    MoveChildWindow(g_hAutomationNav, navX, topY, checkButtonW, topButtonH, TRUE);
    ShowWindow(g_hAutomationNav, SW_SHOWNA);
    SetWindowTextW(g_hAutomationNav, L"check");
    ShowWindow(g_hHomeNav, SW_HIDE);

    if (g_currentPage >= 1 && g_currentPage <= 4)
    {
        ShowWindow(g_hDeleteHolyCheck, SW_HIDE);
        ShowWindow(g_hEnableServices, SW_HIDE);
        ShowWindow(g_hPowerShell, SW_HIDE);
        ShowWindow(g_hCancelDownload, SW_HIDE);
        ShowWindow(g_hDownloadAll, SW_HIDE);
        ShowWindow(g_hCopyEverything1, SW_HIDE);
        ShowWindow(g_hCopyEverything2, SW_HIDE);
        ShowWindow(g_hCopyPowerShell, SW_HIDE);
        ShowWindow(g_hCopySites, SW_HIDE);

        for (HWND tab : g_tabButtons)
            ShowWindow(tab, SW_HIDE);

        for (HWND tile : g_tileButtons)
            ShowWindow(tile, SW_HIDE);

        auto hidePageButtons = []()
        {
            ShowWindow(g_hHomeNav, SW_HIDE);
            ShowWindow(g_hAutomationBam, SW_HIDE);
            ShowWindow(g_hAutomationBamUnsignedOnly, SW_HIDE);
            ShowWindow(g_hAutomationEventVwr, SW_HIDE);
            ShowWindow(g_hAutomationJarPage, SW_HIDE);
            ShowWindow(g_hJarActiv, SW_HIDE);
            ShowWindow(g_hExeDeleted, SW_HIDE);
            ShowWindow(g_hPfDeleted, SW_HIDE);
            ShowWindow(g_hMcLogs, SW_HIDE);
            ShowWindow(g_hPage4Nav, SW_HIDE);
            ShowWindow(g_hModHash, SW_HIDE);
            ShowWindow(g_hModFullScan, SW_HIDE);
            ShowWindow(g_hDllUnsigned, SW_HIDE);
            ShowWindow(g_hDllActivity, SW_HIDE);
            ShowWindow(g_hPage5Nav, SW_HIDE);
            ShowWindow(g_hInjGenCheck, SW_HIDE);
            ShowWindow(g_hEventEdit, SW_HIDE);
            ShowWindow(g_hJarEdit, SW_HIDE);
            ShowWindow(g_hExeEdit, SW_HIDE);
            ShowWindow(g_hPfEdit, SW_HIDE);
            ShowWindow(g_hMcEdit, SW_HIDE);
        };

        hidePageButtons();

        if (g_currentPage == 1)
        {
            RECT scanButton = GetAutomationBamButtonRect(rc);
            RECT fullScanButton = GetModFullScanButtonRect(rc);
            RECT eventButton = GetAutomationEventButtonRect(rc);
            RECT jarPageButton = GetAutomationJarPageButtonRect(rc);

            RECT page4Button = jarPageButton;
            page4Button.right = page4Button.left - topGap;
            page4Button.left = page4Button.right - navButtonW;

            RECT page5Button = page4Button;
            page5Button.right = page5Button.left - topGap;
            page5Button.left = page5Button.right - navButtonW;

            MoveChildWindow(g_hModHash, scanButton.left, scanButton.top, scanButton.right - scanButton.left, scanButton.bottom - scanButton.top, TRUE);
            MoveChildWindow(g_hModFullScan, fullScanButton.left, fullScanButton.top, fullScanButton.right - fullScanButton.left, fullScanButton.bottom - fullScanButton.top, TRUE);
            MoveChildWindow(g_hAutomationJarPage, jarPageButton.left, jarPageButton.top, jarPageButton.right - jarPageButton.left, jarPageButton.bottom - jarPageButton.top, TRUE);
            MoveChildWindow(g_hPage4Nav, page4Button.left, page4Button.top, page4Button.right - page4Button.left, page4Button.bottom - page4Button.top, TRUE);
            MoveChildWindow(g_hPage5Nav, page5Button.left, page5Button.top, page5Button.right - page5Button.left, page5Button.bottom - page5Button.top, TRUE);

            RECT eventPanel = GetAutomationEventPanelRect(rc);
            MoveChildWindow(g_hEventEdit, eventPanel.left + 10, eventPanel.top + 30, eventPanel.right - eventPanel.left - 22, eventPanel.bottom - eventPanel.top - 40, TRUE);
            ShowScrollBar(g_hEventEdit, SB_BOTH, FALSE);
            SetWindowTextW(g_hAutomationNav, L"check");
            SetWindowTextW(g_hModHash, L"scan");
            SetWindowTextW(g_hModFullScan, L"fullscan");
            SetWindowTextW(g_hAutomationJarPage, L"dll | jar");
            SetWindowTextW(g_hPage4Nav, L"");
            SetWindowTextW(g_hPage5Nav, L"injection");

            ShowWindow(g_hAutomationBam, SW_HIDE);
            ShowWindow(g_hModHash, SW_SHOWNA);
            ShowWindow(g_hModFullScan, SW_SHOWNA);
            ShowWindow(g_hAutomationBamUnsignedOnly, SW_HIDE);
            ShowWindow(g_hAutomationEventVwr, SW_HIDE);
            ShowWindow(g_hAutomationJarPage, SW_SHOWNA);
            ShowWindow(g_hPage4Nav, SW_HIDE);
            if (page5Button.left > AUTOMATION_PANEL_X) ShowWindow(g_hPage5Nav, SW_SHOWNA);
            ShowWindow(g_hEventEdit, SW_SHOWNA);
        }
        else if (g_currentPage == 2)
        {
            RECT jarButton = GetJarActivButtonRect(rc);
            MoveChildWindow(g_hJarActiv, jarButton.left, jarButton.top, jarButton.right - jarButton.left, jarButton.bottom - jarButton.top, TRUE);

            RECT jarPanel = GetJarActivityPanelRect(rc);
            MoveChildWindow(g_hJarEdit, jarPanel.left + 10, jarPanel.top + 30, jarPanel.right - jarPanel.left - 34, jarPanel.bottom - jarPanel.top - 40, TRUE);
            SetWindowTextW(g_hJarActiv, L"scan");
            ShowWindow(g_hJarActiv, SW_SHOWNA);
            ShowWindow(g_hDllActivity, SW_HIDE);
            ShowWindow(g_hExeDeleted, SW_HIDE);
            ShowWindow(g_hPfDeleted, SW_HIDE);
            ShowWindow(g_hMcLogs, SW_HIDE);
            ShowWindow(g_hJarEdit, SW_HIDE);
            ShowWindow(g_hExeEdit, SW_HIDE);
            ShowWindow(g_hPfEdit, SW_HIDE);
            ShowWindow(g_hMcEdit, SW_HIDE);
        }
        else if (g_currentPage == 3)
        {
            ShowWindow(g_hModHash, SW_HIDE);
            ShowWindow(g_hModFullScan, SW_HIDE);
            ShowWindow(g_hDllUnsigned, SW_HIDE);
        }
        else if (g_currentPage == 4)
        {
            RECT injButton = GetInjGenButtonRect(rc);
            MoveChildWindow(g_hInjGenCheck, injButton.left, injButton.top, injButton.right - injButton.left, injButton.bottom - injButton.top, TRUE);
            ShowWindow(g_hInjGenCheck, SW_SHOWNA);

        }

        auto showTopNavAll = [&]()
        {
            int x = navX;
            auto place = [&](HWND h, const wchar_t* text)
            {
                if (!h) return;
                MoveChildWindow(h, x, topY, navButtonW, topButtonH, TRUE);
                SetWindowTextW(h, text);
                ShowWindow(h, SW_SHOWNA);
                x -= navButtonW + topGap;
            };
            place(g_hHomeNav, L"Главная");
            place(g_hPage5Nav, L"injection");
            ShowWindow(g_hPage4Nav, SW_HIDE);
            place(g_hAutomationJarPage, L"dll | jar");
            place(g_hAutomationNav, L"check");
        };
        showTopNavAll();

        InvalidateRect(g_hHomeNav, nullptr, TRUE);
        InvalidateRect(g_hAutomationNav, nullptr, TRUE);
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

        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return;
    }

    ShowWindow(g_hHomeNav, SW_HIDE);
    ShowWindow(g_hAutomationBam, SW_HIDE);
    ShowWindow(g_hAutomationBamUnsignedOnly, SW_HIDE);
    ShowWindow(g_hAutomationEventVwr, SW_HIDE);
    ShowWindow(g_hAutomationJarPage, SW_HIDE);
    ShowWindow(g_hJarActiv, SW_HIDE);
    ShowWindow(g_hExeDeleted, SW_HIDE);
    ShowWindow(g_hPfDeleted, SW_HIDE);
    ShowWindow(g_hMcLogs, SW_HIDE);
    ShowWindow(g_hPage4Nav, SW_HIDE);
    ShowWindow(g_hModHash, SW_HIDE);
    ShowWindow(g_hModFullScan, SW_HIDE);
    ShowWindow(g_hDllUnsigned, SW_HIDE);
    ShowWindow(g_hDllActivity, SW_HIDE);
    ShowWindow(g_hPage5Nav, SW_HIDE);
    ShowWindow(g_hInjGenCheck, SW_HIDE);
    ShowWindow(g_hEventEdit, SW_HIDE);
    ShowWindow(g_hJarEdit, SW_HIDE);
    ShowWindow(g_hExeEdit, SW_HIDE);
    ShowWindow(g_hPfEdit, SW_HIDE);
    ShowWindow(g_hMcEdit, SW_HIDE);

    MoveChildWindow(g_hDeleteHolyCheck, 28, 22, 118, 34, TRUE);
    ShowWindow(g_hDeleteHolyCheck, SW_SHOWNA);

    const int topButtonW = 112;
    int powerX = navX - topGap - topButtonW;
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
    MoveChildWindow(g_hCopySites, copyX, copyY + 56, COPY_BUTTON_W, COPY_BUTTON_H, TRUE);
    ShowWindow(g_hCopyEverything1, SW_SHOWNA);
    ShowWindow(g_hCopyEverything2, SW_SHOWNA);
    ShowWindow(g_hCopyPowerShell, SW_HIDE);
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
    InvalidateRect(g_hAutomationNav, nullptr, TRUE);
    InvalidateRect(g_hAutomationBamUnsignedOnly, nullptr, TRUE);
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

    if (id == IDC_AUTOMATION_BAM_UNSIGNED_ONLY)
    {
        bool checked = g_bamUnsignedOnly.load();
        bool pressed = (d->itemState & ODS_SELECTED) != 0;
        bool disabled = ((d->itemState & ODS_DISABLED) != 0) || !IsWindowEnabled(d->hwndItem);

        PaintParentSceneForButton(d->hwndItem, hdc);

        RECT box = r;
        box.left += 2;
        box.top += 4;
        box.right = box.left + 16;
        box.bottom = box.top + 16;

        COLORREF borderColor = disabled ? RGB(70, 70, 76) : RGB(120, 128, 146);
        COLORREF fill = pressed ? RGB(58, 60, 70) : RGB(34, 36, 42);
        HBRUSH br = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
        HGDIOBJ oldBrush = SelectObject(hdc, br);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, box.left, box.top, box.right, box.bottom, 5, 5);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(br);

        if (checked)
        {
            HPEN checkPen = CreatePen(PS_SOLID, 2, disabled ? RGB(120, 120, 126) : RGB(170, 205, 255));
            HGDIOBJ oldCheckPen = SelectObject(hdc, checkPen);
            MoveToEx(hdc, box.left + 4, box.top + 8, nullptr);
            LineTo(hdc, box.left + 7, box.top + 12);
            LineTo(hdc, box.left + 13, box.top + 4);
            SelectObject(hdc, oldCheckPen);
            DeleteObject(checkPen);
        }

        RECT textRect = r;
        textRect.left = box.right + 7;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, disabled ? RGB(145, 145, 152) : TEXT);
        HGDIOBJ oldFont = SelectObject(hdc, g_hSmallFont ? g_hSmallFont : g_hFont);
        DrawTextW(hdc, L"not signed only", -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, oldFont);
        return;
    }

    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    bool disabled = ((d->itemState & ODS_DISABLED) != 0) || !IsWindowEnabled(d->hwndItem);
    bool selected = false;

    if (id >= IDC_TAB_BASE && id < IDC_TAB_BASE + TAB_COUNT)
        selected = (g_currentPage == 0 && id - IDC_TAB_BASE == g_currentTab);

    if (id == IDC_AUTOMATION_NAV && g_currentPage == 1)
        selected = true;

    if (id == IDC_AUTOMATION_JAR_PAGE && g_currentPage == 2)
        selected = true;
    if (id == IDC_PAGE4_NAV && g_currentPage == 3)
        selected = true;
    if (id == IDC_PAGE5_NAV && g_currentPage == 4)
        selected = true;

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

    if (!disabled && (id == IDC_POWERSHELL || id == IDC_ENABLE_SERVICES || id == IDC_DELETE_HOLYCHECK ||
        id == IDC_AUTOMATION_NAV || id == IDC_AUTOMATION_BAM || id == IDC_AUTOMATION_EVENTVWR ||
id == IDC_AUTOMATION_JAR_PAGE || id == IDC_JAR_ACTIV ||
        id == IDC_EXE_DELETED || id == IDC_PF_DELETED || id == IDC_PAGE4_NAV ||
        id == IDC_MOD_HASH || id == IDC_MOD_FULLSCAN || id == IDC_DLL_UNSIGNED || id == IDC_DLL_ACTIVITY || id == IDC_PAGE5_NAV || id == IDC_INJGEN_CHECK))
    {
        if (selected)
        {
            fill = pressed ? RGB(82, 84, 94) : RGB(66, 68, 76);
            borderColor = RGB(142, 148, 160);
        }
        else
        {
            fill = pressed ? RGB(70, 72, 82) : RGB(52, 54, 62);
            borderColor = RGB(92, 98, 112);
        }
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

static std::wstring BuildMinecraftPowerShellScript()
{
    std::wstring script;
    script.reserve(36000);

    script += LR"HCPS($ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
function Out($s){[Console]::WriteLine([string]$s)}
try{
    Out 'Команда: Remove-Module PSReadLine -Force'
    Remove-Module PSReadLine -Force -ErrorAction SilentlyContinue
    Out 'Команда активирована'
}catch{
    Out ('Команда не активирована: '+$_.Exception.Message)
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

    script += L"\n$script:HolyCheckerCheckScript='" + EscapePowerShellSingleQuoted(std::wstring(POWERSHELL_TXT)) + L"'\n";

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

function AddFastPath([System.Collections.Generic.List[string]]$list,[string]$p){
    $n=N $p
    if([string]::IsNullOrWhiteSpace($n)){return}
    [void]$list.Add($n)
}
function GetFastPaths(){
    $a=N $env:APPDATA
    $u=N $env:USERPROFILE
    $list=[System.Collections.Generic.List[string]]::new()
    if(-not [string]::IsNullOrWhiteSpace($a)){
        AddFastPath $list (JoinP @($a,'.tlauncher','legacy','Minecraft','game'))
        AddFastPath $list (JoinP @($a,'.minecraft'))
    }
    if(-not [string]::IsNullOrWhiteSpace($a)){
        AddFastPath $list (JoinP @($u,'.lunarclient'))
    }
    AddFastPath $list 'C:\PulseVisuals\game'
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
        Out 'Введите exit для обновления списка.'
        while($true){
            $raw=ReadLongLine '> '
            if($null -eq $raw){return 'menu'}
            if($raw.Trim().ToLowerInvariant() -eq 'exit'){return 'menu'}
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
        if($t -eq 'exit'){return 'menu'}
        $idx=0
        if([int]::TryParse($t,[ref]$idx) -and $idx -ge 1 -and $idx -le $items.Count){
            $chosen=$items[$idx-1]
            try{$ExecutionContext.SessionState.Path.SetLocation($chosen)}catch{try{[System.IO.Directory]::SetCurrentDirectory($chosen)}catch{}}
            Out ('Выбран путь: '+$chosen)
            Out 'Запуск PowerShell-скрипта check...'
            try{
                $result=[scriptblock]::Create($script:HolyCheckerCheckScript).Invoke()
                if($null -ne $result){foreach($r in $result){if($null -ne $r){Out $r}}}
                Out 'PowerShell-скрипт check завершил работу.'
            }catch{
                Out ('Ошибка запуска PowerShell-скрипта check: '+$_.Exception.Message)
            }
            return 'menu'
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
    ClearScreen
    RunFast
    $next=ChooseFolder
    if($next -eq 'shell'){CommandLoop}
    Out ''
}
)HCPS";

    return script;
}

static std::wstring GetHolyCheckerMinecraftScriptPath()
{
    wchar_t tempPath[MAX_PATH]{};
    DWORD len = GetTempPathW(ARRAYSIZE(tempPath), tempPath);

    if (len == 0 || len >= ARRAYSIZE(tempPath))
        return L"HolyChecker_minecraft_check.ps1";

    return CombinePath(tempPath, L"HolyChecker_minecraft_check.ps1");
}

static void OpenPowerShellMinecraft()
{
    std::wstring script = BuildMinecraftPowerShellScript();
    std::wstring scriptPath = GetHolyCheckerMinecraftScriptPath();

    if (!WriteUtf8TextFile(scriptPath, script.c_str()))
    {
        PostLog(L"PowerShell не открыт: не удалось создать временный скрипт");
        return;
    }

    std::wstring powerShellExe = GetSystemToolPath(L"WindowsPowerShell\\v1.0\\powershell.exe");
    std::wstring command = L"-NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -File " + QuoteProcessArgument(scriptPath);
    std::wstring profile = GetCurrentUserProfilePath();
    const wchar_t* workingDir = DirectoryExists(profile) ? profile.c_str() : nullptr;

    HINSTANCE result = ShellExecuteW(
        g_hMain,
        L"runas",
        powerShellExe.c_str(),
        command.c_str(),
        workingDir,
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(result) <= 32)
        PostLog(L"PowerShell не открыт: ошибка запуска");
    else
        PostLog(L"PowerShell открыт: выберите путь для запуска check");
}

static void OpenCmdEnableServices()
{
    std::wstring command =
        L"/c sc config PcaSVC start= auto & sc start PcaSVC & "
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

