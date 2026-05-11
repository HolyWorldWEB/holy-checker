// 05_fallback_sources.inc
// SoftPortal/HolyWorld fallback search and candidate URLs.
// Original lines: 2023-2856

static std::wstring NormalizeSoftPortalSearchTerm(std::wstring s)
{
    for (wchar_t& ch : s)
    {
        if (ch == L'-' || ch == L'_' || ch == L'.' || ch == L'+' || ch == L'(' || ch == L')')
            ch = L' ';
    }

    while (!s.empty() && iswspace(s.front()))
        s.erase(s.begin());

    while (!s.empty() && iswspace(s.back()))
        s.pop_back();

    std::wstring out;
    bool lastSpace = false;

    for (wchar_t ch : s)
    {
        bool space = iswspace(ch) != 0;

        if (space)
        {
            if (!lastSpace)
                out.push_back(L' ');
        }
        else
        {
            out.push_back(ch);
        }

        lastSpace = space;
    }

    return out;
}

static void AddUniqueString(std::vector<std::wstring>& values, const std::wstring& value)
{
    if (value.empty())
        return;

    std::wstring lowerValue = ToLower(value);

    for (const auto& existing : values)
    {
        if (ToLower(existing) == lowerValue)
            return;
    }

    values.push_back(value);
}

static std::vector<std::wstring> GetSoftPortalSearchTerms(const DownloadItem& item)
{
    std::vector<std::wstring> terms;

    AddUniqueString(terms, item.name);
    AddUniqueString(terms, NormalizeSoftPortalSearchTerm(item.name));
    AddUniqueString(terms, RemoveExtension(item.file));
    AddUniqueString(terms, NormalizeSoftPortalSearchTerm(RemoveExtension(item.file)));

    std::wstring lowerName = ToLower(item.name);

    if (lowerName.find(L"system") != std::wstring::npos && lowerName.find(L"informer") != std::wstring::npos)
    {
        AddUniqueString(terms, L"System Informer");
        AddUniqueString(terms, L"Process Hacker");
    }
    else if (lowerName.find(L"browserdownloads") != std::wstring::npos)
    {
        AddUniqueString(terms, L"Browser Downloads View");
    }
    else if (lowerName.find(L"browsinghistory") != std::wstring::npos)
    {
        AddUniqueString(terms, L"Browsing History View");
    }
    else if (lowerName.find(L"recentfiles") != std::wstring::npos)
    {
        AddUniqueString(terms, L"Recent Files View");
    }
    else if (lowerName.find(L"executedprograms") != std::wstring::npos)
    {
        AddUniqueString(terms, L"Executed Programs List");
    }
    else if (lowerName.find(L"usbdeview") != std::wstring::npos)
    {
        AddUniqueString(terms, L"USBDeview");
        AddUniqueString(terms, L"USB Deview");
    }
    else if (lowerName.find(L"usbdrivelog") != std::wstring::npos)
    {
        AddUniqueString(terms, L"USB Drive Log");
    }
    else if (lowerName.find(L"windeflog") != std::wstring::npos)
    {
        AddUniqueString(terms, L"WinDefLogView");
        AddUniqueString(terms, L"WinDef Log View");
    }
    else if (lowerName.find(L"alternatestream") != std::wstring::npos)
    {
        AddUniqueString(terms, L"AlternateStreamView");
        AddUniqueString(terms, L"Alternate Stream View");
    }
    else if (lowerName.find(L"winprefetch") != std::wstring::npos)
    {
        AddUniqueString(terms, L"WinPrefetchView");
        AddUniqueString(terms, L"Win Prefetch View");
    }
    else if (lowerName.find(L"winrar") != std::wstring::npos)
    {
        AddUniqueString(terms, L"WinRAR");
    }
    else if (lowerName.find(L"recuva") != std::wstring::npos)
    {
        AddUniqueString(terms, L"Recuva");
        AddUniqueString(terms, L"Piriform Recuva");
    }

    return terms;
}

static std::vector<std::wstring> GetSoftPortalFallbackUrls(const DownloadItem& item)
{
    std::vector<std::wstring> urls;
    std::wstring name = ToLower(item.name);

    // Stable known SoftPortal pages for programs that do not always have direct CDN links.
    if (name.find(L"recuva") != std::wstring::npos)
    {
        urls.push_back(L"https://www.softportal.com/getsoft-5667-recuva-3.html");
        urls.push_back(L"https://www.softportal.com/get-5667-recuva.html");
        urls.push_back(L"https://www.softportal.com/en/recuva/windows/download");
    }

    std::vector<std::wstring> terms = GetSoftPortalSearchTerms(item);

    // Generic SoftPortal fallback is intentionally compact: if the official URL is down,
    // try the strongest 2 names and 2 endpoints instead of crawling many pages slowly.
    int termCount = 0;
    for (const auto& term : terms)
    {
        if (termCount >= 2)
            break;

        std::wstring q = UrlEncodeUtf8(term);

        AddUniqueString(urls, L"https://www.softportal.com/search.html?text=" + q);
        AddUniqueString(urls, L"https://www.softportal.com/en/search?q=" + q);
        ++termCount;
    }

    return urls;
}



static std::string AsciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static std::string WideLowerUtf8(const std::wstring& value)
{
    return AsciiLower(WideToUtf8(ToLower(value)));
}

static bool StringContainsAny(const std::string& text, const std::vector<std::string>& needles)
{
    for (const auto& needle : needles)
    {
        if (!needle.empty() && text.find(needle) != std::string::npos)
            return true;
    }

    return false;
}

static std::vector<std::string> GetItemSearchNeedles(const DownloadItem& item)
{
    std::vector<std::string> needles;
    auto add = [&](std::string v)
    {
        v = AsciiLower(v);
        if (!v.empty() && std::find(needles.begin(), needles.end(), v) == needles.end())
            needles.push_back(v);
    };

    add(WideToUtf8(item.name));
    add(WideToUtf8(RemoveExtension(item.file)));

    std::string compactName = WideToUtf8(item.name);
    compactName.erase(std::remove_if(compactName.begin(), compactName.end(), [](unsigned char c)
    {
        return c == ' ' || c == '-' || c == '_' || c == '.';
    }), compactName.end());
    add(compactName);

    std::wstring lower = ToLower(item.name);

    if (IsRegistryExplorerItem(item))
    {
        add("registryexplorer");
        add("registry explorer");
        add("registry");
    }
    else if (IsJumpListExplorerItem(item))
    {
        add("jumplistexplorer");
        add("jumplist explorer");
        add("jumplist");
        add("jump list");
        add("jumplistexplore");
    }
    else if (IsSimpleUnlockerItem(item))
    {
        add("simpleunlocker");
        add("simple unlocker");
        add("simpleunlocker_release");
        add("unlocker");
    }
    else if (IsWinRarItem(item))
    {
        add("winrar");
        add("win-rar");
        add("rarlab");
    }
    else if (IsSystemInformerItem(item))
    {
        add("systeminformer");
        add("system informer");
    }

    return needles;
}

static bool IsDownloadLikeUrl(const std::string& lowerUrl)
{
    return lowerUrl.find(".zip") != std::string::npos ||
        lowerUrl.find(".exe") != std::string::npos ||
        lowerUrl.find(".jar") != std::string::npos ||
        lowerUrl.find(".7z") != std::string::npos ||
        lowerUrl.find(".rar") != std::string::npos ||
        lowerUrl.find(".msi") != std::string::npos ||
        lowerUrl.find("/download") != std::string::npos ||
        lowerUrl.find("download?") != std::string::npos ||
        lowerUrl.find("/api/") != std::string::npos;
}

static std::wstring FindBestDownloadLinkForItem(
    const std::wstring& baseUrl,
    const std::vector<unsigned char>& bytes,
    const DownloadItem& item)
{
    std::string html(bytes.begin(), bytes.end());
    std::string lowerHtml = AsciiLower(html);
    std::vector<std::string> needles = GetItemSearchNeedles(item);

    int bestScore = 0;
    std::wstring bestUrl;

    auto scoreCandidate = [&](const std::string& rawValue, size_t pos)
    {
        if (rawValue.empty())
            return;

        std::string raw = rawValue;
        while (!raw.empty() && (raw.front() == '\'' || raw.front() == '"' || raw.front() == '`' || raw.front() == '('))
            raw.erase(raw.begin());
        while (!raw.empty() && (raw.back() == '\'' || raw.back() == '"' || raw.back() == '`' || raw.back() == ',' || raw.back() == ';' || raw.back() == ')' || raw.back() == ']'))
            raw.pop_back();

        std::string lowerRaw = AsciiLower(raw);

        if (lowerRaw.empty() || lowerRaw == "#" || lowerRaw.rfind("javascript:", 0) == 0 || lowerRaw.rfind("mailto:", 0) == 0)
            return;

        bool maybeDownloadRoute =
            lowerRaw.find("download") != std::string::npos ||
            lowerRaw.find("application") != std::string::npos ||
            lowerRaw.find("file") != std::string::npos ||
            lowerRaw.find("api") != std::string::npos ||
            IsDownloadLikeUrl(lowerRaw);

        if (!maybeDownloadRoute)
            return;

        std::wstring absolute = MakeAbsoluteUrl(baseUrl, CleanHtmlUrl(raw));
        std::string absoluteLower = WideLowerUtf8(absolute);

        size_t ctxStart = pos > 1800 ? pos - 1800 : 0;
        size_t ctxEnd = std::min(html.size(), pos + raw.size() + 1800);
        std::string context = lowerHtml.substr(ctxStart, ctxEnd - ctxStart);

        if (absoluteLower == "https://mods.holyworld.me/applications" ||
            absoluteLower == "https://mods.holyworld.me/download" ||
            absoluteLower.find("/login") != std::string::npos ||
            absoluteLower.find("/staff") != std::string::npos ||
            absoluteLower.find("/news") != std::string::npos ||
            absoluteLower.find("/mods-review") != std::string::npos)
        {
            return;
        }

        bool externalHttp = absoluteLower.find("mods.holyworld.me") == std::string::npos && absoluteLower.rfind("http", 0) == 0;
        if (externalHttp && !StringContainsAny(absoluteLower, needles) && !StringContainsAny(context, needles))
            return;

        int score = 0;

        if (StringContainsAny(absoluteLower, needles))
            score += 130;

        if (StringContainsAny(context, needles))
            score += 110;

        if (absoluteLower.find("mods.holyworld.me") != std::string::npos)
            score += 45;

        if (IsDownloadLikeUrl(absoluteLower))
            score += 45;

        if (absoluteLower.find("download") != std::string::npos)
            score += 35;

        if (absoluteLower.find("/api/") != std::string::npos)
            score += 20;

        if (absoluteLower.find(".zip") != std::string::npos || absoluteLower.find(".exe") != std::string::npos)
            score += 35;

        if (absoluteLower.find("login") != std::string::npos || absoluteLower.find("logout") != std::string::npos || absoluteLower.find("register") != std::string::npos)
            score -= 200;

        if (score > bestScore && score >= 125)
        {
            bestScore = score;
            bestUrl = absolute;
        }
    };

    std::regex attrRe(
        R"HC((?:href|src|data-url|data-href|data-link|data-download|data-download-url|download-url)\s*=\s*["']([^"']+)["'])HC",
        std::regex_constants::icase
    );

    std::smatch m;
    auto begin = html.cbegin();
    auto end = html.cend();

    while (std::regex_search(begin, end, m, attrRe))
    {
        size_t pos = static_cast<size_t>(m.position(1) + std::distance(html.cbegin(), begin));
        scoreCandidate(m[1].str(), pos);
        begin = m.suffix().first;
    }

    std::regex jsonUrlRe(
        R"HC((?:"|')?(?:url|href|link|file|path|download|downloadUrl|download_url|downloadLink|download_link)(?:"|')?\s*[:=]\s*(?:"|')([^"']+)(?:"|'))HC",
        std::regex_constants::icase
    );

    begin = html.cbegin();
    while (std::regex_search(begin, end, m, jsonUrlRe))
    {
        size_t pos = static_cast<size_t>(m.position(1) + std::distance(html.cbegin(), begin));
        scoreCandidate(m[1].str(), pos);
        begin = m.suffix().first;
    }

    std::regex plainUrlRe(
        R"HC((https?://[^\s"'<>`]+(?:\.zip|\.exe|\.jar|\.7z|\.rar|\.msi|download|api|applications)[^\s"'<>`]*))HC",
        std::regex_constants::icase
    );

    begin = html.cbegin();
    while (std::regex_search(begin, end, m, plainUrlRe))
    {
        size_t pos = static_cast<size_t>(m.position(1) + std::distance(html.cbegin(), begin));
        scoreCandidate(m[1].str(), pos);
        begin = m.suffix().first;
    }

    std::regex relativeRe(
        R"HC((/(?:api/)?(?:applications?|download|downloads|files)[^\s"'<>`]+))HC",
        std::regex_constants::icase
    );

    begin = html.cbegin();
    while (std::regex_search(begin, end, m, relativeRe))
    {
        size_t pos = static_cast<size_t>(m.position(1) + std::distance(html.cbegin(), begin));
        scoreCandidate(m[1].str(), pos);
        begin = m.suffix().first;
    }

    return bestUrl;
}


static bool FetchUrlBytes(const std::wstring& url, const std::wstring& displayName, std::vector<unsigned char>& out)
{
    out.clear();

    if (IsCancelRequested())
        return false;

    ParsedUrl p;
    if (!ParseUrl(url, p))
        return false;

    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    std::shared_ptr<HttpHandleSlot> sessionSlot;
    std::shared_ptr<HttpHandleSlot> connectSlot;
    std::shared_ptr<HttpHandleSlot> requestSlot;
    bool ok = false;

    session = WinHttpOpen(
        L"Mozilla/5.0 HolyCheckDownloader/9.2",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session)
        return false;

    sessionSlot = TrackHttpHandle(session);

    int resolveMs = 3500;
    int connectMs = 3500;
    int sendMs = 5000;
    int receiveMs = 12000;
    GetHttpTimeouts(displayName, url, resolveMs, connectMs, sendMs, receiveMs);
    WinHttpSetTimeouts(session, resolveMs, connectMs, sendMs, std::min(receiveMs, 12000));

    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));

    connect = WinHttpConnect(session, p.host.c_str(), p.port, 0);
    if (!connect)
        goto cleanup;

    connectSlot = TrackHttpHandle(connect);

    request = WinHttpOpenRequest(
        connect,
        L"GET",
        p.pathAndQuery.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        p.https ? WINHTTP_FLAG_SECURE : 0
    );

    if (!request)
        goto cleanup;

    requestSlot = TrackHttpHandle(request);

    WinHttpAddRequestHeaders(
        request,
        L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36\r\n"
        L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,application/json,*/*;q=0.8\r\n"
        L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6\r\n"
        L"Referer: https://mods.holyworld.me/applications\r\n"
        L"Cache-Control: no-cache\r\n"
        L"Pragma: no-cache\r\n",
        static_cast<DWORD>(-1),
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE
    );

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto cleanup;

    if (!WinHttpReceiveResponse(request, nullptr))
        goto cleanup;

    {
        DWORD status = QueryStatus(request);
        if (status < 200 || status >= 300)
            goto cleanup;
    }

    ok = ReadAll(request, out, HTML_READ_LIMIT) && !out.empty();

cleanup:
    CloseTrackedHttpHandle(requestSlot);
    CloseTrackedHttpHandle(connectSlot);
    CloseTrackedHttpHandle(sessionSlot);
    return ok && !IsCancelRequested();
}

static std::vector<std::wstring> GetHolyWorldCandidateUrls(const DownloadItem& item)
{
    std::vector<std::wstring> urls;

    auto addFor = [&](const std::wstring& canonical, const std::wstring& file)
    {
        std::wstring encodedCanonical = UrlEncodeUtf8(canonical);
        std::wstring encodedFile = UrlEncodeUtf8(file);

        // Keep this list short: first parse the real site page, then try only likely direct routes.
        AddUniqueString(urls, L"https://mods.holyworld.me/download/" + canonical);
        AddUniqueString(urls, L"https://mods.holyworld.me/download/" + file);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications/download/" + canonical);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications/download/" + file);
        AddUniqueString(urls, L"https://mods.holyworld.me/api/download/" + canonical);
        AddUniqueString(urls, L"https://mods.holyworld.me/api/download/" + file);
        AddUniqueString(urls, L"https://mods.holyworld.me/api/applications/download/" + canonical);
        AddUniqueString(urls, L"https://mods.holyworld.me/api/applications/download/" + file);
        AddUniqueString(urls, L"https://mods.holyworld.me/download?name=" + encodedCanonical);
        AddUniqueString(urls, L"https://mods.holyworld.me/download?file=" + encodedFile);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications?download=" + encodedCanonical);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications?file=" + encodedFile);
        AddUniqueString(urls, L"https://mods.holyworld.me/files/" + file);
        AddUniqueString(urls, L"https://mods.holyworld.me/downloads/" + file);
        AddUniqueString(urls, L"https://mods.holyworld.me/storage/applications/" + file);
    };

    if (IsRegistryExplorerItem(item))
    {
        addFor(L"RegistryExplorer", L"RegistryExplorer.zip");
        addFor(L"registryexplorer", L"registryexplorer.zip");
        addFor(L"registry-explorer", L"RegistryExplorer.zip");
    }
    else if (IsJumpListExplorerItem(item))
    {
        addFor(L"JumpListExplorer", L"JumpListExplorer.zip");
        addFor(L"JumpListExplore", L"JumpListExplorer.zip");
        addFor(L"jumplistexplorer", L"jumplistexplorer.zip");
        addFor(L"jumplist-explorer", L"JumpListExplorer.zip");
    }
    else if (IsSimpleUnlockerItem(item))
    {
        addFor(L"SimpleUnlocker", L"simpleunlocker_release.zip");
        addFor(L"Simpleunlocker", L"simpleunlocker_release.zip");
        addFor(L"simpleunlocker", L"simpleunlocker_release.zip");
        addFor(L"simple-unlocker", L"simpleunlocker_release.zip");
    }

    return urls;
}

static std::vector<std::wstring> FindHolyWorldCandidateUrlsInPage(
    const std::wstring& baseUrl,
    const std::vector<unsigned char>& bytes,
    const DownloadItem& item)
{
    std::vector<std::wstring> urls;
    std::string html(bytes.begin(), bytes.end());
    std::string lowerHtml = AsciiLower(html);
    std::vector<std::string> needles = GetItemSearchNeedles(item);

    auto addRoutesForValue = [&](const std::string& value)
    {
        if (value.empty() || value.size() > 96)
            return;

        std::string v = value;
        while (!v.empty() && (v.front() == '\'' || v.front() == '"' || v.front() == '`' || v.front() == '('))
            v.erase(v.begin());
        while (!v.empty() && (v.back() == '\'' || v.back() == '"' || v.back() == '`' || v.back() == ',' || v.back() == ';' || v.back() == ')' || v.back() == ']'))
            v.pop_back();

        if (v.empty() || v == "true" || v == "false" || v == "null" || v == "download" || v == "applications")
            return;

        std::wstring w = Utf8ToWide(v);
        std::wstring e = UrlEncodeUtf8(w);
        std::string lowerValue = AsciiLower(v);

        if (lowerValue.rfind("http://", 0) == 0 || lowerValue.rfind("https://", 0) == 0 || lowerValue.rfind("/", 0) == 0)
        {
            AddUniqueString(urls, MakeAbsoluteUrl(baseUrl, w));
            return;
        }

        AddUniqueString(urls, L"https://mods.holyworld.me/download/" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications/download/" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/api/download/" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/api/applications/download/" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/download?id=" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/download?name=" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/download?file=" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications?id=" + e);
        AddUniqueString(urls, L"https://mods.holyworld.me/applications?download=" + e);

        if (lowerValue.find(".zip") != std::string::npos || lowerValue.find(".exe") != std::string::npos)
        {
            AddUniqueString(urls, L"https://mods.holyworld.me/files/" + w);
            AddUniqueString(urls, L"https://mods.holyworld.me/downloads/" + w);
            AddUniqueString(urls, L"https://mods.holyworld.me/storage/applications/" + w);
        }
    };

    std::regex keyValueRe(
        R"HC((?:\"|')?(?:id|_id|appId|applicationId|slug|key|name|file|filename|download|downloadUrl|download_url)(?:\"|')?\s*[:=]\s*(?:\"|')?([A-Za-z0-9_.\-]{1,96})(?:\"|')?)HC",
        std::regex_constants::icase
    );

    for (const auto& needle : needles)
    {
        if (needle.empty())
            continue;

        size_t pos = lowerHtml.find(needle);
        while (pos != std::string::npos)
        {
            size_t ctxStart = pos > 2200 ? pos - 2200 : 0;
            size_t ctxEnd = std::min(html.size(), pos + needle.size() + 2200);
            std::string ctx = html.substr(ctxStart, ctxEnd - ctxStart);

            std::smatch m;
            auto begin = ctx.cbegin();
            auto end = ctx.cend();
            while (std::regex_search(begin, end, m, keyValueRe))
            {
                addRoutesForValue(m[1].str());
                begin = m.suffix().first;
            }

            pos = lowerHtml.find(needle, pos + needle.size());
        }
    }

    return urls;
}

static bool TryHolyWorldDownloadUrl(const DownloadItem& item, const std::wstring& targetPath, const std::wstring& url, bool allowPowerShell)
{
    if (IsCancelRequested() || url.empty())
        return false;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    if (DownloadRecursiveValidated(item, url, targetPath))
        return true;

    if (DownloadCurlValidated(item, url, targetPath))
        return true;

    if (allowPowerShell && DownloadPowerShellValidated(item, url, targetPath))
        return true;

    return false;
}

static bool TryHolyWorldFallback(const DownloadItem& item, const std::wstring& targetPath)
{
    if (!(IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item) || IsSimpleUnlockerItem(item)))
        return false;

    PostLog(L"HolyWorld: " + item.name);

    const std::wstring pages[] =
    {
        L"https://mods.holyworld.me/applications",
        L"https://mods.holyworld.me/download"
    };

    for (const auto& page : pages)
    {
        if (IsCancelRequested())
            return false;

        std::vector<unsigned char> html;
        if (!FetchUrlBytes(page, item.name, html))
            continue;

        std::wstring direct = FindBestDownloadLinkForItem(page, html, item);
        if (!direct.empty())
        {
            PostLog(L"HolyWorld link: " + item.name);
            if (TryHolyWorldDownloadUrl(item, targetPath, direct, true))
                return true;
        }

        std::vector<std::wstring> pageUrls = FindHolyWorldCandidateUrlsInPage(page, html, item);
        for (const auto& url : pageUrls)
        {
            if (IsCancelRequested())
                return false;

            if (TryHolyWorldDownloadUrl(item, targetPath, url, false))
                return true;
        }
    }

    std::vector<std::wstring> candidates = GetHolyWorldCandidateUrls(item);
    for (const auto& url : candidates)
    {
        if (IsCancelRequested())
            return false;

        if (TryHolyWorldDownloadUrl(item, targetPath, url, false))
            return true;
    }

    return false;
}


static std::vector<std::wstring> GetFastFallbackUrls(const DownloadItem& item)
{
    std::vector<std::wstring> urls;
    std::wstring name = ToLower(item.name);

    if (IsSystemInformerItem(item))
    {
        // Сначала старая рабочая прямая ссылка, затем страницы/зеркала.
        AddUniqueString(urls, L"https://downloads.sourceforge.net/project/systeminformer/systeminformer-3.2.25011-release-setup.exe");
        AddUniqueString(urls, L"https://sourceforge.net/projects/systeminformer/files/systeminformer-3.2.25011-release-setup.exe/download");
        AddUniqueString(urls, L"https://github.com/winsiderss/systeminformer/releases/download/v3.2.25011.2103/systeminformer-3.2.25011-release-setup.exe");
        AddUniqueString(urls, L"https://systeminformer.sourceforge.io/canary/systeminformer-3.2.25011-setup.exe");
    }
    else if (IsWinRarItem(item))
    {
        // Прямая ссылка RARLAB на русский x64 installer, если postdownload вернул HTML.
        AddUniqueString(urls, L"https://www.win-rar.com/postdownload.html?&L=4");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-722ru.exe");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-722.exe");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-721ru.exe");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-721.exe");
    }
    else if (name.find(L"recuva") != std::wstring::npos)
    {
        AddUniqueString(urls, L"https://www.softportal.com/getsoft-5667-recuva-3.html");
        AddUniqueString(urls, L"https://download.ccleaner.com/rcsetup154.exe");
        AddUniqueString(urls, L"https://www.ccleaner.com/recuva/download/standard");
        AddUniqueString(urls, L"https://sourceforge.net/projects/recuva/files/rcsetup154.exe/download");
        AddUniqueString(urls, L"https://downloads.sourceforge.net/project/recuva/rcsetup154.exe");
    }

    return urls;
}

static bool TryFastFallbackUrls(const DownloadItem& item, const std::wstring& targetPath)
{
    std::vector<std::wstring> urls = GetFastFallbackUrls(item);

    for (const auto& url : urls)
    {
        if (IsCancelRequested())
            return false;

        if (ToLower(url) == ToLower(item.url))
            continue;

        DeleteFileW(targetPath.c_str());
        DeleteFileW((targetPath + L".part").c_str());

        PostLog(L"Быстрый резерв: " + item.name);

        if (IsEricZimmermanLargeItem(item))
        {
            // For large Eric Zimmerman archives curl handles CDN redirects and
            // slow streams closer to how the browser behaves, so try it first.
            if (DownloadCurlValidated(item, url, targetPath))
                return true;

            if (IsCancelRequested())
                return false;

            if (DownloadRecursiveValidated(item, url, targetPath))
                return true;
        }
        else
        {
            if (DownloadRecursiveValidated(item, url, targetPath))
                return true;

            if (IsCancelRequested())
                return false;

            if (DownloadCurlValidated(item, url, targetPath))
                return true;
        }

        if (IsCancelRequested())
            return false;

        if (DownloadPowerShellValidated(item, url, targetPath))
            return true;
    }

    return false;
}

static bool TrySoftPortalFallback(const DownloadItem& item, const std::wstring& targetPath)
{
    if (IsCancelRequested() || IsFastFailItem(item))
        return false;

    PostLog(L"Альтернатива: поиск " + item.name);

    std::vector<std::wstring> urls = GetSoftPortalFallbackUrls(item);

    for (const auto& url : urls)
    {
        if (IsCancelRequested())
            return false;

        DeleteFileW(targetPath.c_str());
        DeleteFileW((targetPath + L".part").c_str());

        if (DownloadRecursiveValidated(item, url, targetPath))
        {
            PostLog(L"Альтернатива: скачано " + item.name);
            return true;
        }

        if (IsCancelRequested())
            return false;

        if (DownloadPowerShellValidated(item, url, targetPath))
        {
            PostLog(L"Альтернатива: скачано " + item.name);
            return true;
        }
    }

    PostLog(L"Альтернатива: не найдена " + item.name);
    return false;
}

