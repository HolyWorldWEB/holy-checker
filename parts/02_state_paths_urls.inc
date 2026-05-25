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

static bool IsAllowedDownloadLogLine(const std::wstring& line)
{
    return line.rfind(L"скачиваеться:", 0) == 0 ||
        line.rfind(L"скачивается:", 0) == 0 ||
        line.rfind(L"скачалось:", 0) == 0 ||
        line.rfind(L"не скачалось:", 0) == 0;
}

static void PostLog(const std::wstring& line)
{
    if (!IsAllowedDownloadLogLine(line))
        return;

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

static std::wstring FormatOneDecimal(unsigned long long value, unsigned long long divisor)
{
    if (divisor == 0)
        return std::to_wstring(value);

    unsigned long long whole = value / divisor;
    unsigned long long tenth = ((value % divisor) * 10ull) / divisor;
    return std::to_wstring(whole) + L"." + std::to_wstring(tenth);
}

static std::wstring FormatBytes(unsigned long long bytes)
{
    if (bytes >= 1024ull * 1024ull * 1024ull)
        return FormatOneDecimal(bytes, 1024ull * 1024ull * 1024ull) + L" ГБ";

    if (bytes >= 1024ull * 1024ull)
        return FormatOneDecimal(bytes, 1024ull * 1024ull) + L" МБ";

    if (bytes >= 1024ull)
        return FormatOneDecimal(bytes, 1024ull) + L" КБ";

    return std::to_wstring(bytes) + L" Б";
}

static std::wstring FormatSpeedBps(unsigned long long bps)
{
    if (bps == 0)
        return L"0 КБ/с";

    if (bps >= 1024ull * 1024ull)
        return FormatOneDecimal(bps, 1024ull * 1024ull) + L" МБ/с";

    if (bps >= 1024ull)
        return FormatOneDecimal(bps, 1024ull) + L" КБ/с";

    return std::to_wstring(bps) + L" Б/с";
}

static std::wstring FormatSpeedForLog(unsigned long long bps)
{
    if (bps == 0)
        return L"0 КБ/сек";

    if (bps >= 1024ull * 1024ull)
        return FormatOneDecimal(bps, 1024ull * 1024ull) + L" МБ/сек";

    if (bps >= 1024ull)
        return FormatOneDecimal(bps, 1024ull) + L" КБ/сек";

    return std::to_wstring(bps) + L" Б/сек";
}

static bool StartsWithDownloadLineForName(const std::wstring& line, const std::wstring& name)
{
    const std::wstring p1 = L"скачиваеться: " + name;
    const std::wstring p2 = L"скачивается: " + name;

    return line.rfind(p1, 0) == 0 || line.rfind(p2, 0) == 0;
}

static void UpsertLiveDownloadLogLine(const std::wstring& name, unsigned long long speedBps)
{
    if (name.empty())
        return;

    std::wstring line = L"скачиваеться: " + name;

    if (speedBps > 0)
        line += L" | " + FormatSpeedForLog(speedBps);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);

        for (auto it = g_logLines.rbegin(); it != g_logLines.rend(); ++it)
        {
            if (StartsWithDownloadLineForName(*it, name))
            {
                *it = ShortLogLine(line);
                ClampLogScrollOffsetLocked();
                InvalidateBottomPanel();
                return;
            }
        }
    }

    PushLogLine(line);
    InvalidateBottomPanel();
}

static void RemoveLiveDownloadLogLine(const std::wstring& name)
{
    if (name.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(g_logMutex);

        g_logLines.erase(
            std::remove_if(g_logLines.begin(), g_logLines.end(), [&](const std::wstring& line)
            {
                return StartsWithDownloadLineForName(line, name);
            }),
            g_logLines.end()
        );

        ClampLogScrollOffsetLocked();
    }

    InvalidateBottomPanel();
}

static std::wstring FormatElapsedMs(ULONGLONG elapsedMs)
{
    unsigned long long sec = static_cast<unsigned long long>(elapsedMs / 1000ull);

    if (sec < 60)
        return std::to_wstring(sec) + L"с";

    unsigned long long min = sec / 60ull;
    sec %= 60ull;
    return std::to_wstring(min) + L"м " + std::to_wstring(sec) + L"с";
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

        auto it = std::find_if(g_activeDownloads.begin(), g_activeDownloads.end(), [&](const ActiveDownloadInfo& info)
        {
            return info.name == name;
        });

        if (it == g_activeDownloads.end())
        {
            ActiveDownloadInfo info;
            info.name = name;
            info.method = L"ожидание";
            info.startedTick = GetTickCount64();
            info.updatedTick = info.startedTick;
            g_activeDownloads.push_back(info);
        }
    }

    InvalidateBottomPanel();
}

static void UpdateActiveDownloadStatus(
    const std::wstring& name,
    const std::wstring& method,
    size_t linkIndex,
    size_t linkTotal,
    unsigned long long bytes,
    unsigned long long speedBps)
{
    {
        std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);

        auto it = std::find_if(g_activeDownloads.begin(), g_activeDownloads.end(), [&](const ActiveDownloadInfo& info)
        {
            return info.name == name;
        });

        if (it == g_activeDownloads.end())
        {
            ActiveDownloadInfo info;
            info.name = name;
            info.startedTick = GetTickCount64();
            info.updatedTick = info.startedTick;
            g_activeDownloads.push_back(info);
            it = g_activeDownloads.end() - 1;
        }

        it->method = method.empty() ? L"загрузка" : method;
        it->linkIndex = linkIndex;
        it->linkTotal = linkTotal;
        it->bytes = bytes;
        it->speedBps = speedBps;
        it->updatedTick = GetTickCount64();
    }

    UpsertLiveDownloadLogLine(name, speedBps);
    InvalidateBottomPanel();
}

static void RemoveActiveDownload(const std::wstring& name)
{
    {
        std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);
        g_activeDownloads.erase(
            std::remove_if(g_activeDownloads.begin(), g_activeDownloads.end(), [&](const ActiveDownloadInfo& info)
            {
                return info.name == name;
            }),
            g_activeDownloads.end()
        );
    }

    RemoveLiveDownloadLogLine(name);
    InvalidateBottomPanel();
}

static std::wstring GetActiveDownloadsText()
{
    std::lock_guard<std::mutex> lock(g_activeDownloadsMutex);

    if (g_activeDownloads.empty())
        return L"";

    unsigned long long totalSpeed = 0;

    for (const auto& info : g_activeDownloads)
        totalSpeed += info.speedBps;

    std::wstring result = L"Сейчас: ";

    if (totalSpeed > 0)
    {
        result += L"итого ";
        result += FormatSpeedBps(totalSpeed);
        result += L" | ";
    }

    const size_t maxShown = 3;

    for (size_t i = 0; i < g_activeDownloads.size() && i < maxShown; ++i)
    {
        if (i > 0)
            result += L"; ";

        const ActiveDownloadInfo& info = g_activeDownloads[i];
        result += L"скачивается: ";
        result += info.name;
        result += L" | ";
        result += FormatSpeedBps(info.speedBps);

        if (!info.method.empty() && info.method != L"ожидание")
        {
            result += L" | ";
            result += info.method;
        }

        if (info.linkTotal > 0 && info.linkIndex > 0)
        {
            result += L" | ссылка ";
            result += std::to_wstring(info.linkIndex);
            result += L"/";
            result += std::to_wstring(info.linkTotal);
        }
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


static bool IsProcessHackerItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"processhacker") != std::wstring::npos ||
        (name.find(L"process") != std::wstring::npos && name.find(L"hacker") != std::wstring::npos);
}

static bool IsBamParserItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    std::wstring file = ToLower(item.file);
    return name.find(L"bamparser") != std::wstring::npos ||
        name.find(L"bam parser") != std::wstring::npos ||
        file.find(L"bamparser") != std::wstring::npos;
}

static bool IsDetectItEasyItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    std::wstring file = ToLower(item.file);

    return name.find(L"detect it easy") != std::wstring::npos ||
        file.find(L"die_win64") != std::wstring::npos ||
        file.find(L"detect-it-easy") != std::wstring::npos;
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


static bool IsGitHubUrl(const std::wstring& url)
{
    std::wstring lower = ToLower(url);
    return lower.find(L"github.com") != std::wstring::npos ||
        lower.find(L"githubusercontent.com") != std::wstring::npos ||
        lower.find(L"release-assets.githubusercontent.com") != std::wstring::npos ||
        lower.find(L"objects.githubusercontent.com") != std::wstring::npos;
}

static bool IsSourceForgeUrl(const std::wstring& url)
{
    std::wstring lower = ToLower(url);
    return lower.find(L"sourceforge.net") != std::wstring::npos ||
        lower.find(L"downloads.sourceforge.net") != std::wstring::npos ||
        lower.find(L"sourceforge.io") != std::wstring::npos;
}

static bool IsSourceForgeDownloadWaitUrl(const std::wstring& url)
{
    std::wstring lower = ToLower(url);

    if (lower.find(L"downloads.sourceforge.net") != std::wstring::npos)
        return true;

    if (lower.find(L"sourceforge.net") != std::wstring::npos &&
        (lower.find(L"/download") != std::wstring::npos || lower.find(L"/projects/") != std::wstring::npos))
    {
        return true;
    }

    if (lower.find(L"sourceforge.io") != std::wstring::npos &&
        (lower.find(L".exe") != std::wstring::npos || lower.find(L"/download") != std::wstring::npos))
    {
        return true;
    }

    return false;
}

static bool IsEricZimmermanLargeItem(const DownloadItem& item)
{
    return IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item);
}

static DWORD GetDirectDownloadTimeoutMs(const DownloadItem& item)
{
    if (IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item))
        return 900000;

    if (IsSimpleUnlockerItem(item))
        return 180000;

    if (IsSystemInformerItem(item))
        return 600000;

    if (IsWinRarItem(item))
        return 180000;

    return IsFastFailItem(item) ? 30000 : 300000;
}

static void GetHttpTimeouts(const std::wstring& displayName, const std::wstring& url, int& resolveMs, int& connectMs, int& sendMs, int& receiveMs)
{
    std::wstring lowerUrl = ToLower(url);

    if (IsEricZimmermanUrl(url))
    {
        resolveMs = 10000;
        connectMs = 15000;
        sendMs = 15000;
        receiveMs = 15000;
    }
    else if (IsFastFailDownload(displayName, url))
    {
        resolveMs = 3500;
        connectMs = 3500;
        sendMs = 5000;
        receiveMs = 8000;
    }
    else
    {
        resolveMs = 4500;
        connectMs = 4500;
        sendMs = 8000;
        receiveMs = 10000;
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

struct MinecraftFolderCandidate
{
    std::wstring path;
    bool exists = false;
    bool isRoot = false;
};

static void AddUniquePath(std::vector<std::wstring>& paths, const std::wstring& path)
{
    if (path.empty())
        return;

    std::wstring normalized = GetFullPathSafe(path);

    if (normalized.empty())
        normalized = path;

    std::wstring lower = ToLower(normalized);

    for (const std::wstring& existing : paths)
    {
        if (ToLower(GetFullPathSafe(existing)) == lower)
            return;
    }

    paths.push_back(normalized);
}

static std::wstring GetLastPathPart(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();

    size_t pos = path.find_last_of(L"\\/");

    if (pos == std::wstring::npos)
        return path;

    return path.substr(pos + 1);
}

static bool IsExcludedMinecraftFolderName(const std::wstring& name)
{
    std::wstring lower = ToLower(name);

    return lower == L"labymod" || lower == L"labymod-neo";
}

static bool IsExcludedMinecraftFolderPath(const std::wstring& path)
{
    return IsExcludedMinecraftFolderName(GetLastPathPart(path));
}

static std::wstring GetCurrentUserProfilePath()
{
    wchar_t profile[MAX_PATH * 4]{};
    DWORD profileLen = GetEnvironmentVariableW(L"USERPROFILE", profile, ARRAYSIZE(profile));

    if (profileLen == 0 || profileLen >= ARRAYSIZE(profile))
        return L"";

    return GetFullPathSafe(profile);
}

static bool IsPathUnderFolder(const std::wstring& path, const std::wstring& root)
{
    std::wstring fullPath = ToLower(GetFullPathSafe(path));
    std::wstring fullRoot = ToLower(GetFullPathSafe(root));

    if (fullPath.empty() || fullRoot.empty())
        return false;

    while (!fullRoot.empty() && (fullRoot.back() == L'\\' || fullRoot.back() == L'/'))
        fullRoot.pop_back();

    if (fullPath == fullRoot)
        return true;

    std::wstring rootWithSlash = fullRoot + L"\\";
    return fullPath.rfind(rootWithSlash, 0) == 0;
}

static bool IsPathUnderCurrentUserProfile(const std::wstring& path)
{
    std::wstring profile = GetCurrentUserProfilePath();

    if (profile.empty())
        return false;

    return IsPathUnderFolder(path, profile);
}

static bool HasJarFileRecursiveLimited(const std::wstring& folder, int depth)
{
    if (depth < 0 || !DirectoryExists(folder))
        return false;

    WIN32_FIND_DATAW data{};
    std::wstring mask = CombinePath(folder, L"*");
    HANDLE h = FindFirstFileW(mask.c_str(), &data);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;

    do
    {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        std::wstring child = CombinePath(folder, data.cFileName);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;

            if (IsExcludedMinecraftFolderName(data.cFileName))
                continue;

            if (HasJarFileRecursiveLimited(child, depth - 1))
            {
                found = true;
                break;
            }
        }
        else
        {
            std::wstring name = ToLower(data.cFileName);

            if (name.size() >= 4 && name.substr(name.size() - 4) == L".jar")
            {
                found = true;
                break;
            }
        }
    }
    while (FindNextFileW(h, &data));

    FindClose(h);
    return found;
}

static bool IsStrictMinecraftRootFolder(const std::wstring& path)
{
    if (!DirectoryExists(path))
        return false;

    if (IsExcludedMinecraftFolderPath(path))
        return false;

    if (!IsPathUnderCurrentUserProfile(path))
        return false;

    std::wstring assets = CombinePath(path, L"assets");
    std::wstring versions = CombinePath(path, L"versions");
    std::wstring logs = CombinePath(path, L"logs");

    if (!DirectoryExists(assets))
        return false;

    if (!DirectoryExists(versions))
        return false;

    if (!DirectoryExists(logs))
        return false;

    if (!HasJarFileRecursiveLimited(versions, 3))
        return false;

    return true;
}

static bool HasAnyStrictMinecraftMarker(const std::wstring& path)
{
    if (!DirectoryExists(path) || IsExcludedMinecraftFolderPath(path) || !IsPathUnderCurrentUserProfile(path))
        return false;

    return DirectoryExists(CombinePath(path, L"assets")) ||
        DirectoryExists(CombinePath(path, L"versions")) ||
        DirectoryExists(CombinePath(path, L"logs"));
}

static bool IsMinecraftSearchContainerName(const std::wstring& name)
{
    std::wstring lower = ToLower(name);

    if (IsExcludedMinecraftFolderName(lower))
        return false;

    return lower == L".minecraft" ||
        lower == L"minecraft" ||
        lower == L"game" ||
        lower == L"games" ||
        lower == L"instances" ||
        lower == L"launchers" ||
        lower == L"appdata" ||
        lower == L"roaming" ||
        lower == L"local" ||
        lower == L"documents" ||
        lower == L"downloads" ||
        lower.find(L"minecraft") != std::wstring::npos ||
        lower.find(L"launcher") != std::wstring::npos ||
        lower.find(L"tlauncher") != std::wstring::npos ||
        lower.find(L"lunar") != std::wstring::npos ||
        lower.find(L"badlion") != std::wstring::npos ||
        lower.find(L"feather") != std::wstring::npos ||
        lower.find(L"curseforge") != std::wstring::npos ||
        lower.find(L"modrinth") != std::wstring::npos ||
        lower.find(L"technic") != std::wstring::npos ||
        lower.find(L"atlauncher") != std::wstring::npos ||
        lower.find(L"polymc") != std::wstring::npos ||
        lower.find(L"prism") != std::wstring::npos ||
        lower.find(L"gdlauncher") != std::wstring::npos ||
        lower.find(L"multimc") != std::wstring::npos ||
        lower.find(L"pulsevisuals") != std::wstring::npos;
}

static void ScanMinecraftFoldersRecursive(const std::wstring& root, int depth, int& visited, std::vector<std::wstring>& paths)
{
    if (depth < 0 || visited > 6000 || !DirectoryExists(root) || IsExcludedMinecraftFolderPath(root) || !IsPathUnderCurrentUserProfile(root))
        return;

    ++visited;

    if (IsStrictMinecraftRootFolder(root))
        AddUniquePath(paths, root);

    if (depth == 0)
        return;

    WIN32_FIND_DATAW data{};
    std::wstring mask = CombinePath(root, L"*");
    HANDLE h = FindFirstFileW(mask.c_str(), &data);

    if (h == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;

        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        if (IsExcludedMinecraftFolderName(data.cFileName))
            continue;

        std::wstring child = CombinePath(root, data.cFileName);

        if (IsMinecraftSearchContainerName(data.cFileName) || HasAnyStrictMinecraftMarker(child))
            ScanMinecraftFoldersRecursive(child, depth - 1, visited, paths);
    }
    while (FindNextFileW(h, &data));

    FindClose(h);
}

static std::vector<std::wstring> BuildMinecraftCandidatePaths()
{
    std::vector<std::wstring> paths;
    std::wstring profile = GetCurrentUserProfilePath();

    if (profile.empty())
        return paths;

    wchar_t appData[MAX_PATH * 4]{};
    DWORD appLen = GetEnvironmentVariableW(L"APPDATA", appData, ARRAYSIZE(appData));

    if (appLen > 0 && appLen < ARRAYSIZE(appData) && IsPathUnderFolder(appData, profile))
        AddUniquePath(paths, CombinePath(appData, L".minecraft"));

    wchar_t localAppData[MAX_PATH * 4]{};
    DWORD localLen = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, ARRAYSIZE(localAppData));

    AddUniquePath(paths, CombinePath(CombinePath(profile, L"AppData\\Roaming"), L".minecraft"));
    AddUniquePath(paths, CombinePath(CombinePath(profile, L"curseforge"), L"minecraft\\Instances"));
    AddUniquePath(paths, CombinePath(CombinePath(profile, L"Documents"), L"Curse\\Minecraft\\Instances"));
    AddUniquePath(paths, CombinePath(profile, L"Games\\Minecraft"));
    AddUniquePath(paths, CombinePath(profile, L"Minecraft"));
    AddUniquePath(paths, CombinePath(profile, L"game"));

    int visited = 0;

    ScanMinecraftFoldersRecursive(profile, 7, visited, paths);

    if (localLen > 0 && localLen < ARRAYSIZE(localAppData) && IsPathUnderFolder(localAppData, profile))
        ScanMinecraftFoldersRecursive(localAppData, 6, visited, paths);

    return paths;
}

static std::vector<MinecraftFolderCandidate> GetMinecraftFolderCandidates()
{
    std::vector<MinecraftFolderCandidate> result;
    std::vector<std::wstring> paths = BuildMinecraftCandidatePaths();

    for (const std::wstring& path : paths)
    {
        if (IsExcludedMinecraftFolderPath(path) || !IsPathUnderCurrentUserProfile(path))
            continue;

        MinecraftFolderCandidate item;
        item.path = path;
        item.exists = DirectoryExists(path);
        item.isRoot = IsStrictMinecraftRootFolder(path);
        result.push_back(item);
    }

    std::stable_sort(result.begin(), result.end(), [](const MinecraftFolderCandidate& a, const MinecraftFolderCandidate& b)
    {
        if (a.isRoot != b.isRoot)
            return a.isRoot;

        if (a.exists != b.exists)
            return a.exists;

        return ToLower(a.path) < ToLower(b.path);
    });

    return result;
}

static std::wstring GetMinecraftFolder()
{
    std::vector<MinecraftFolderCandidate> candidates = GetMinecraftFolderCandidates();

    for (const MinecraftFolderCandidate& candidate : candidates)
    {
        if (candidate.exists && candidate.isRoot)
            return candidate.path;
    }

    std::wstring profile = GetCurrentUserProfilePath();

    if (!profile.empty() && DirectoryExists(profile))
        return profile;

    return candidates.empty() ? std::wstring() : candidates.front().path;
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

