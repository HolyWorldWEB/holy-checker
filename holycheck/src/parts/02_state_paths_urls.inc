// 02_state_paths_urls.inc
// Логи, состояние загрузок, работа с путями, строками и URL.
// Original lines: 387-1185

static void PostProgress(int value)
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(value), 0);
}

static std::wstring ShortLogLine(std::wstring line)
{
    const size_t maxLen = 124;

    for (wchar_t& ch : line)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            ch = L' ';
    }

    if (line.size() > maxLen)
        line = line.substr(0, maxLen - 3) + L"...";

    return line;
}

static void PushLogLine(const std::wstring& line)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    bool wasAtBottom = g_logScrollOffset == 0;

    g_logLines.push_back(ShortLogLine(line));

    while (g_logLines.size() > LOG_MAX_LINES)
    {
        g_logLines.erase(g_logLines.begin());

        if (g_logScrollOffset > 0)
            --g_logScrollOffset;
    }

    if (!wasAtBottom)
        ++g_logScrollOffset;

    ClampLogScrollOffsetLocked();
}

static void ClearDownloadLog()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logLines.clear();
    g_logScrollOffset = 0;
}

static void PostLog(const std::wstring& line)
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    std::wstring* heapLine = new (std::nothrow) std::wstring(line);

    if (!heapLine)
        return;

    if (!PostMessageW(hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(heapLine)))
        delete heapLine;
}

static std::shared_ptr<HttpHandleSlot> TrackHttpHandle(HINTERNET handle)
{
    if (!handle)
        return nullptr;

    auto slot = std::make_shared<HttpHandleSlot>();
    slot->handle = handle;

    std::lock_guard<std::mutex> lock(g_httpHandlesMutex);
    g_httpHandles.push_back(slot);
    return slot;
}

static void CloseTrackedHttpHandle(std::shared_ptr<HttpHandleSlot>& slot)
{
    HINTERNET handle = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_httpHandlesMutex);

        if (!slot || slot->closed)
            return;

        handle = slot->handle;
        slot->handle = nullptr;
        slot->closed = true;

        g_httpHandles.erase(
            std::remove(g_httpHandles.begin(), g_httpHandles.end(), slot),
            g_httpHandles.end()
        );
    }

    if (handle)
        WinHttpCloseHandle(handle);
}

static void AbortActiveHttpHandles()
{
    std::vector<std::shared_ptr<HttpHandleSlot>> handles;

    {
        std::lock_guard<std::mutex> lock(g_httpHandlesMutex);
        handles = g_httpHandles;
    }

    for (auto& slot : handles)
        CloseTrackedHttpHandle(slot);
}

static bool IsCancelRequested()
{
    return g_cancelRequested.load();
}

static void RequestCancelDownloads()
{
    bool hasWork = g_working.load() || g_activeSingleDownloads.load() > 0;

    if (!hasWork)
        return;

    g_cancelRequested = true;
    AbortActiveHttpHandles();
    PostLog(L"Отмена загрузок...");
}


static bool IsItemBusyIndex(int index)
{
    std::lock_guard<std::mutex> lock(g_itemBusyMutex);

    if (index < 0 || index >= static_cast<int>(g_itemBusy.size()))
        return false;

    return g_itemBusy[index] != 0;
}

static void SetItemBusyIndex(int index, bool busy)
{
    std::lock_guard<std::mutex> lock(g_itemBusyMutex);

    if (index < 0 || index >= static_cast<int>(g_itemBusy.size()))
        return;

    g_itemBusy[index] = busy ? 1 : 0;
}

static void SetTileEnabledByIndex(int index, bool enabled)
{
    if (index < 0 || index >= static_cast<int>(g_tileButtons.size()))
        return;

    EnableWindow(g_tileButtons[index], enabled ? TRUE : FALSE);
    InvalidateRect(g_tileButtons[index], nullptr, FALSE);
}

static bool IsUiThread()
{
    return g_uiThreadId != 0 && GetCurrentThreadId() == g_uiThreadId;
}

static void PostUiRefresh()
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    PostMessageW(hwnd, WM_APP_REFRESH, 0, 0);
}

static void InvalidateBottomPanel()
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    if (!IsUiThread())
    {
        PostUiRefresh();
        return;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT panelRect{ 0, rc.bottom - LOG_INVALIDATE_HEIGHT, rc.right, rc.bottom };
    InvalidateRect(hwnd, &panelRect, FALSE);
}

static void ClearActiveDownloads()
{
    std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);
    g_activeDownloads.clear();
}

static void AddActiveDownload(const std::wstring& name)
{
    {
        std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);

        if (std::find(g_activeDownloads.begin(), g_activeDownloads.end(), name) == g_activeDownloads.end())
            g_activeDownloads.push_back(name);
    }

    InvalidateBottomPanel();
}

static void RemoveActiveDownload(const std::wstring& name)
{
    {
        std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);
        g_activeDownloads.erase(std::remove(g_activeDownloads.begin(), g_activeDownloads.end(), name), g_activeDownloads.end());
    }

    InvalidateBottomPanel();
}

static std::wstring GetActiveDownloadsText()
{
    std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);

    if (g_activeDownloads.empty())
        return L"";

    std::wstring result = L"Сейчас: ";

    const size_t maxShown = 3;

    for (size_t i = 0; i < g_activeDownloads.size() && i < maxShown; ++i)
    {
        if (i > 0)
            result += L", ";

        result += g_activeDownloads[i];
    }

    if (g_activeDownloads.size() > maxShown)
        result += L" +" + std::to_wstring(g_activeDownloads.size() - maxShown);

    return result;
}

static std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c)
    {
        return static_cast<wchar_t>(towlower(c));
    });
    return s;
}

static bool EndsWithI(const std::wstring& s, const std::wstring& suffix)
{
    std::wstring a = ToLower(s);
    std::wstring b = ToLower(suffix);

    return a.size() >= b.size() &&
        a.compare(a.size() - b.size(), b.size(), b) == 0;
}

static bool IsFastFailName(const std::wstring& name)
{
    std::wstring lower = ToLower(name);

    return lower.find(L"registryexplorer") != std::wstring::npos ||
        (lower.find(L"registry") != std::wstring::npos && lower.find(L"explorer") != std::wstring::npos) ||
        lower.find(L"jumplistexplorer") != std::wstring::npos ||
        (lower.find(L"jumplist") != std::wstring::npos && lower.find(L"explorer") != std::wstring::npos) ||
        lower.find(L"simpleunlocker") != std::wstring::npos ||
        (lower.find(L"simple") != std::wstring::npos && lower.find(L"unlocker") != std::wstring::npos);
}

static bool IsFastFailItem(const DownloadItem& item)
{
    return IsFastFailName(item.name);
}

static bool IsEricZimmermanUrl(const std::wstring& url)
{
    return ToLower(url).find(L"download.ericzimmermanstools.com") != std::wstring::npos;
}

static bool IsFastFailUrl(const std::wstring& url)
{
    std::wstring lower = ToLower(url);

    // Eric Zimmerman archives are direct but can be large, so they must NOT use
    // the short fast-fail timeout. Browser downloads work because they wait
    // longer; the program should do the same.
    return lower.find(L"simpleunlocker.ds1nc.ru") != std::wstring::npos;
}

static bool IsFastFailDownload(const std::wstring& displayName, const std::wstring& url)
{
    return IsFastFailName(displayName) || IsFastFailUrl(url);
}

static bool IsRegistryExplorerItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"registryexplorer") != std::wstring::npos ||
        (name.find(L"registry") != std::wstring::npos && name.find(L"explorer") != std::wstring::npos);
}

static bool IsJumpListExplorerItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"jumplistexplorer") != std::wstring::npos ||
        (name.find(L"jumplist") != std::wstring::npos && name.find(L"explorer") != std::wstring::npos);
}

static bool IsSimpleUnlockerItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"simpleunlocker") != std::wstring::npos ||
        (name.find(L"simple") != std::wstring::npos && name.find(L"unlocker") != std::wstring::npos);
}

static bool IsWinRarItem(const DownloadItem& item)
{
    return ToLower(item.name).find(L"winrar") != std::wstring::npos;
}

static bool IsSystemInformerItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"systeminformer") != std::wstring::npos ||
        (name.find(L"system") != std::wstring::npos && name.find(L"informer") != std::wstring::npos);
}

static bool IsEricZimmermanLargeItem(const DownloadItem& item)
{
    return IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item);
}

static DWORD GetPowerShellTimeoutMs(const DownloadItem& item)
{
    if (IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item))
        return 180000;

    if (IsSimpleUnlockerItem(item))
        return 60000;

    if (IsWinRarItem(item) || IsSystemInformerItem(item))
        return 45000;

    return IsFastFailItem(item) ? 22000 : 42000;
}

static void GetHttpTimeouts(const std::wstring& displayName, const std::wstring& url, int& resolveMs, int& connectMs, int& sendMs, int& receiveMs)
{
    std::wstring lowerUrl = ToLower(url);

    if (IsEricZimmermanUrl(url))
    {
        // Eric Zimmerman GUI tools are large archives. Short receive timeouts
        // made RegistryExplorer/JumpListExplorer fail or look stuck while the
        // same link worked in a browser. Give the CDN enough time to stream.
        resolveMs = 10000;
        connectMs = 15000;
        sendMs = 15000;
        receiveMs = 600000;
    }
    else if (lowerUrl.find(L"mods.holyworld.me") != std::wstring::npos)
    {
        resolveMs = 3500;
        connectMs = 3500;
        sendMs = 5000;

        if (lowerUrl.find(L"download") != std::wstring::npos ||
            lowerUrl.find(L".zip") != std::wstring::npos ||
            lowerUrl.find(L".exe") != std::wstring::npos ||
            lowerUrl.find(L"file=") != std::wstring::npos ||
            lowerUrl.find(L"app=") != std::wstring::npos ||
            lowerUrl.find(L"name=") != std::wstring::npos)
        {
            receiveMs = 60000;
        }
        else
        {
            receiveMs = 12000;
        }
    }
    else if (IsFastFailDownload(displayName, url))
    {
        resolveMs = 3500;
        connectMs = 3500;
        sendMs = 5000;
        receiveMs = 16000;
    }
    else
    {
        resolveMs = 4500;
        connectMs = 4500;
        sendMs = 8000;
        receiveMs = 26000;
    }
}

static bool FileExists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}


static bool GetFileSizeBytes(const std::wstring& path, unsigned long long& outSize)
{
    outSize = 0;

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0;
    CloseHandle(file);

    if (!ok)
        return false;

    outSize = static_cast<unsigned long long>(size.QuadPart);
    return true;
}

static unsigned long long GetFileSizeBytesOrZero(const std::wstring& path)
{
    unsigned long long size = 0;
    GetFileSizeBytes(path, size);
    return size;
}

static bool ParseUnsignedLongLong(const std::wstring& value, unsigned long long& out)
{
    out = 0;
    if (value.empty())
        return false;

    size_t i = 0;
    while (i < value.size() && iswspace(value[i]))
        ++i;

    bool hasDigit = false;
    for (; i < value.size(); ++i)
    {
        wchar_t ch = value[i];
        if (iswspace(ch))
            break;

        if (ch < L'0' || ch > L'9')
            return false;

        hasDigit = true;
        unsigned long long digit = static_cast<unsigned long long>(ch - L'0');
        if (out > (ULLONG_MAX - digit) / 10)
            return false;

        out = out * 10 + digit;
    }

    return hasDigit;
}

static void LogDownloadAttemptFailure(const std::wstring& displayName, int attempt, int totalAttempts, const DownloadResult& result)
{
    (void)displayName;
    (void)attempt;
    (void)totalAttempts;
    (void)result;

    // Подробные причины ошибок намеренно не выводятся в UI.
    // Итоговый короткий статус уже пишет ProcessItem(): "Не скачалось" / "Отменено".
}

static DWORD GetRetryDelayMs(int attempt)
{
    static const DWORD delays[] = { 0, 1000, 3000, 7000 };
    if (attempt <= 0)
        return 0;

    size_t index = static_cast<size_t>(attempt);
    if (index >= ARRAYSIZE(delays))
        index = ARRAYSIZE(delays) - 1;

    return delays[index];
}

static bool SleepRetryDelay(DWORD delayMs)
{
    DWORD slept = 0;
    while (slept < delayMs)
    {
        if (IsCancelRequested())
            return false;

        DWORD chunk = std::min<DWORD>(150, delayMs - slept);
        Sleep(chunk);
        slept += chunk;
    }

    return true;
}

static bool DirectoryExists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool EnsureDirectory(const std::wstring& path)
{
    if (DirectoryExists(path))
        return true;

    int r = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || DirectoryExists(path);
}

static std::wstring CombinePath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty())
        return b;

    if (b.empty())
        return a;

    if (b.size() >= 2 && b[1] == L':')
        return b;

    if (b[0] == L'\\' || b[0] == L'/')
        return b;

    std::wstring out = a;

    if (!out.empty() && out.back() != L'\\' && out.back() != L'/')
        out.push_back(L'\\');

    out += b;
    return out;
}

static std::wstring RemoveExtension(std::wstring s)
{
    size_t p = s.find_last_of(L'.');
    if (p != std::wstring::npos)
        s = s.substr(0, p);
    return s;
}

static std::wstring GetTabFolderName(int tab)
{
    switch (tab)
    {
    case 0: return L"Мл.сотрудник";
    case 1: return L"Сотрудник";
    case 2: return L"Вед.сотрудник";
    case 3: return L"Доп.программы";
    default: return L"Прочее";
    }
}

static std::wstring BuildHolyCheckFolderPath(bool ensureDirectory)
{
    PWSTR downloads = nullptr;
    std::wstring result;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads)) && downloads)
    {
        result = CombinePath(downloads, L"holycheck");
        CoTaskMemFree(downloads);
    }
    else
    {
        wchar_t profile[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        result = CombinePath(CombinePath(profile, L"Downloads"), L"holycheck");
    }

    if (ensureDirectory)
        EnsureDirectory(result);

    return result;
}

static std::wstring GetHolyCheckFolder()
{
    return BuildHolyCheckFolderPath(true);
}

static std::wstring GetHolyCheckFolderPathOnly()
{
    return BuildHolyCheckFolderPath(false);
}
static std::wstring GetFullPathSafe(const std::wstring& path)
{
    if (path.empty())
        return L"";

    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);

    if (required == 0)
        return path;

    std::wstring out(required, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), required, &out[0], nullptr);

    if (written == 0 || written >= required)
        return path;

    out.resize(written);

    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/'))
        out.pop_back();

    return out;
}

static bool IsSafeHolyCheckDeletionTarget(const std::wstring& folder)
{
    std::wstring full = ToLower(GetFullPathSafe(folder));
    std::wstring expected = ToLower(GetFullPathSafe(GetHolyCheckFolderPathOnly()));

    if (full.empty() || expected.empty())
        return false;

    if (full != expected)
        return false;

    if (full.size() < 12)
        return false;

    return EndsWithI(full, L"\\holycheck") || EndsWithI(full, L"/holycheck");
}

static std::wstring GetMinecraftFolder()
{
    wchar_t appData[MAX_PATH * 4]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, ARRAYSIZE(appData));

    std::wstring result;

    if (len > 0 && len < ARRAYSIZE(appData))
    {
        result = CombinePath(appData, L".minecraft");
    }
    else
    {
        wchar_t profile[MAX_PATH * 4]{};
        DWORD profileLen = GetEnvironmentVariableW(L"USERPROFILE", profile, ARRAYSIZE(profile));

        if (profileLen > 0 && profileLen < ARRAYSIZE(profile))
            result = CombinePath(CombinePath(profile, L"AppData\\Roaming"), L".minecraft");
        else
            result = L".minecraft";
    }

    EnsureDirectory(result);
    return result;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return L"";

    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');

    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);

    return out;
}

static std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty())
        return "";

    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');

    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);

    return out;
}

static std::wstring UrlEncodeUtf8(const std::wstring& s)
{
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::string utf8 = WideToUtf8(s);
    std::wstring out;

    for (unsigned char c : utf8)
    {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<wchar_t>(c));
        }
        else if (c == ' ')
        {
            out += L"%20";
        }
        else
        {
            out.push_back(L'%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }

    return out;
}

static bool ParseUrl(const std::wstring& url, ParsedUrl& out)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);

    wchar_t host[512]{};
    wchar_t path[4096]{};
    wchar_t extra[4096]{};

    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = ARRAYSIZE(extra);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return false;

    out.host.assign(host, uc.dwHostNameLength);
    out.port = uc.nPort;
    out.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    out.pathAndQuery.assign(path, uc.dwUrlPathLength);
    out.pathAndQuery.append(extra, uc.dwExtraInfoLength);

    if (out.pathAndQuery.empty())
        out.pathAndQuery = L"/";

    return !out.host.empty();
}

static std::wstring MakeAbsoluteUrl(const std::wstring& base, const std::wstring& href)
{
    if (href.rfind(L"http://", 0) == 0 || href.rfind(L"https://", 0) == 0)
        return href;

    ParsedUrl p;
    if (!ParseUrl(base, p))
        return href;

    std::wstring origin = (p.https ? L"https://" : L"http://") + p.host;

    if (href.rfind(L"//", 0) == 0)
        return p.https ? L"https:" + href : L"http:" + href;

    if (!href.empty() && href[0] == L'/')
        return origin + href;

    std::wstring dir = p.pathAndQuery;
    size_t q = dir.find(L'?');
    if (q != std::wstring::npos)
        dir = dir.substr(0, q);

    size_t slash = dir.find_last_of(L'/');
    dir = slash != std::wstring::npos ? dir.substr(0, slash + 1) : L"/";

    return origin + dir + href;
}

