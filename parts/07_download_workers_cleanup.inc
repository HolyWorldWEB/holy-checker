static bool DeleteDirectoryTree(const std::wstring& folder);

static void CleanupOldJuniorTextFiles(const std::wstring& tabFolder)
{
    DeleteFileW(CombinePath(tabFolder, L"everything.txt").c_str());
    DeleteFileW(CombinePath(tabFolder, L"journaltrace.txt").c_str());
    DeleteFileW(CombinePath(tabFolder, L"systeminformer.txt").c_str());
}

static size_t DeleteXmlFilesRecursive(const std::wstring& folder)
{
    if (!DirectoryExists(folder))
        return 0;

    if (FileExists(CombinePath(folder, L".extract.ok")))
        return 0;

    size_t deletedCount = 0;
    std::wstring mask = CombinePath(folder, L"*");

    WIN32_FIND_DATAW data{};
    HANDLE hFind = FindFirstFileW(mask.c_str(), &data);

    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        std::wstring path = CombinePath(folder, data.cFileName);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            deletedCount += DeleteXmlFilesRecursive(path);
        }
        else if (EndsWithI(path, L".xml"))
        {
            if (DeleteFileW(path.c_str()))
                ++deletedCount;
        }
    }
    while (FindNextFileW(hFind, &data));

    FindClose(hFind);
    return deletedCount;
}

static void CleanupXmlFiles(const std::wstring& folder)
{
    DeleteXmlFilesRecursive(folder);
}
static bool WaitForExtractedExe(const std::wstring& extractDir, std::wstring& exePath, DWORD timeoutMs = ZIP_EXTRACT_TIMEOUT_MS)
{
    ULONGLONG started = GetTickCount64();

    do
    {
        if (IsCancelRequested())
            return false;

        if (FindFirstExeRecursive(extractDir, exePath))
            return true;

        Sleep(ZIP_EXTRACT_POLL_MS);
    }
    while (GetTickCount64() - started < timeoutMs);

    return false;
}


static bool ShouldExtractOnlyOnClick(const DownloadItem& item)
{
    return IsDetectItEasyItem(item);
}

static bool ShouldSkipXmlCleanupForItem(const DownloadItem& item)
{
    return IsDetectItEasyItem(item);
}

static std::wstring GetExtractionMarkerPath(const std::wstring& extractDir)
{
    return CombinePath(extractDir, L".extract.ok");
}

static void RemoveExtractionMarker(const std::wstring& extractDir)
{
    DeleteFileW(GetExtractionMarkerPath(extractDir).c_str());
}

static bool WriteExtractionMarker(const std::wstring& extractDir)
{
    std::wstring marker = GetExtractionMarkerPath(extractDir);
    HANDLE file = CreateFileW(
        marker.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    const char text[] = "ok\r\n";
    DWORD written = 0;
    bool ok = WriteFile(file, text, static_cast<DWORD>(sizeof(text) - 1), &written, nullptr) &&
        written == sizeof(text) - 1;
    CloseHandle(file);
    return ok;
}

static bool HasCompletedExtraction(const std::wstring& extractDir)
{
    return DirectoryExists(extractDir) && FileExists(GetExtractionMarkerPath(extractDir));
}



static bool IsSpeedSensitiveSequentialItem(const DownloadItem& item)
{
    UNREFERENCED_PARAMETER(item);
    return false;
}


static bool ProcessItem(const DownloadItem& item, const std::wstring& baseFolder, bool extractZip = true)
{
    DebugLogLine(L"download", L"process item start name=" + item.name + L" file=" + item.file +
        L" tab=" + std::to_wstring(item.tab) + L" base=" + baseFolder +
        L" extractZip=" + std::to_wstring(extractZip ? 1 : 0) + L" url=" + item.url);

    if (IsCancelRequested())
    {
        DebugLogLine(L"download", L"process item cancelled before start name=" + item.name);
        return false;
    }

    std::wstring tabFolder = CombinePath(baseFolder, GetTabFolderName(item.tab));

    if (!EnsureDirectory(tabFolder))
    {
        DebugLogLine(L"download", L"process item failed ensure tab folder name=" + item.name + L" folder=" + tabFolder);
        return false;
    }

    if (item.tab == 0)
        CleanupOldJuniorTextFiles(tabFolder);

    std::wstring target = CombinePath(tabFolder, item.file);
    bool zipItem = EndsWithI(target, L".zip");
    bool hadFileBefore = FileExists(target);
    bool downloadedThisRun = false;

    DebugLogLine(L"download", L"process item target=" + target + L" hadFileBefore=" + std::to_wstring(hadFileBefore ? 1 : 0));
    if (hadFileBefore && !ValidateDownloadedFileOrDelete(item, target))
    {
        DebugLogLine(L"download", L"existing file validation failed name=" + item.name + L" target=" + target);
        hadFileBefore = false;
    }

    if (!hadFileBefore)
    {
        AddActiveDownload(item.name);

        bool downloaded = DownloadPrimaryUrlOnlyValidated(item, target);

        RemoveActiveDownload(item.name);

        if (!downloaded)
        {
            DebugLogLine(L"download", L"download failed name=" + item.name + L" target=" + target);
            return false;
        }

        DebugLogLine(L"download", L"download ok name=" + item.name + L" target=" + target + L" size=" + std::to_wstring(GetFileSizeBytesOrZero(target)));
        downloadedThisRun = true;
    }

    if (IsCancelRequested())
    {
        DebugLogLine(L"download", L"process item cancelled after download name=" + item.name);
        return false;
    }

    if (zipItem)
    {
        DeleteFileW(CombinePath(tabFolder, MakeSafeFileStem(item.name) + L".exe").c_str());
    }

    if (extractZip && zipItem && FileExists(target))
    {
        std::wstring extractDir = CombinePath(tabFolder, RemoveExtension(item.file));
        std::wstring exePath;
        bool alreadyExtracted = !downloadedThisRun && HasCompletedExtraction(extractDir) && FindFirstExeRecursive(extractDir, exePath);

        if (!alreadyExtracted)
        {
            RemoveExtractionMarker(extractDir);
            DeleteDirectoryTree(extractDir);
            EnsureDirectory(extractDir);

            DebugLogLine(L"download", L"extract start name=" + item.name + L" zip=" + target + L" dir=" + extractDir);
            if (!ExtractZip(target, extractDir))
            {
                DebugLogLine(L"download", L"extract failed name=" + item.name + L" zip=" + target + L" dir=" + extractDir);
                return false;
            }

            if (!WaitForExtractedExe(extractDir, exePath))
            {
                DebugLogLine(L"download", L"extract exe wait failed name=" + item.name + L" dir=" + extractDir);
                return false;
            }
            DebugLogLine(L"download", L"extract ok name=" + item.name + L" exe=" + exePath);

            if (!ShouldSkipXmlCleanupForItem(item))
                CleanupXmlFiles(extractDir);

            WriteExtractionMarker(extractDir);
        }
    }

    if (!ShouldSkipXmlCleanupForItem(item))
        CleanupXmlFiles(tabFolder);

    {
        bool ok = FileExists(target) && !IsCancelRequested();
        DebugLogLine(L"download", L"process item finish name=" + item.name + L" ok=" + std::to_wstring(ok ? 1 : 0) +
            L" target=" + target + L" size=" + std::to_wstring(GetFileSizeBytesOrZero(target)));
        return ok;
    }
}


static void PostDownloadSummaryLine(const std::wstring& prefix, const std::vector<std::wstring>& names)
{
    if (names.empty())
    {
        PostLog(prefix + L"нет");
        return;
    }

    const size_t maxLineLen = 150;
    std::wstring line = prefix;

    for (const auto& name : names)
    {
        std::wstring part = name;

        if (line.size() > prefix.size())
            part = L", " + part;

        if (line.size() + part.size() > maxLineLen && line.size() > prefix.size())
        {
            PostLog(line);
            line = prefix + name;
        }
        else
        {
            line += part;
        }
    }

    if (line.size() > prefix.size())
        PostLog(line);
}

static void PostDownloadSummary(const std::vector<std::wstring>& downloaded, const std::vector<std::wstring>& failed)
{
    UNREFERENCED_PARAMETER(downloaded);
    PostDownloadSummaryLine(L"не скачалось: итог: ", failed);
}


static unsigned long long GetEstimatedDownloadOrderBytes(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    std::wstring file = ToLower(item.file);
    std::wstring url = ToLower(item.url);

    if (name.find(L"recentfilesview") != std::wstring::npos) return 90ull * 1024ull;
    if (name.find(L"lastactivityview") != std::wstring::npos) return 120ull * 1024ull;
    if (name.find(L"executedprogramslist") != std::wstring::npos) return 120ull * 1024ull;
    if (name.find(L"browserdownloadsview") != std::wstring::npos) return 200ull * 1024ull;
    if (name.find(L"browsinghistoryview") != std::wstring::npos) return 220ull * 1024ull;
    if (name.find(L"regscanner") != std::wstring::npos) return 250ull * 1024ull;
    if (name.find(L"winprefetchview") != std::wstring::npos) return 350ull * 1024ull;
    if (name.find(L"alternatestreamview") != std::wstring::npos) return 450ull * 1024ull;
    if (name.find(L"windeflogview") != std::wstring::npos) return 500ull * 1024ull;
    if (name.find(L"everything") != std::wstring::npos) return 6ull * 1024ull * 1024ull;
    if (name.find(L"recuva") != std::wstring::npos) return 25ull * 1024ull * 1024ull;
    if (name.find(L"detect") != std::wstring::npos) return 30ull * 1024ull * 1024ull;
    if (name.find(L"winrar") != std::wstring::npos) return 4ull * 1024ull * 1024ull;
    if (name.find(L"systeminformer") != std::wstring::npos) return 20ull * 1024ull * 1024ull;
    if (name.find(L"processhacker") != std::wstring::npos) return 4ull * 1024ull * 1024ull;
    if (name.find(L"registryexplorer") != std::wstring::npos) return 75ull * 1024ull * 1024ull;
    if (name.find(L"jumplistexplorer") != std::wstring::npos) return 70ull * 1024ull * 1024ull;
    if (name.find(L"volatility") != std::wstring::npos) return 80ull * 1024ull * 1024ull;
    if (name.find(L"simpleunlocker") != std::wstring::npos) return 20ull * 1024ull * 1024ull;

    if (EndsWithI(file, L".zip")) return 2ull * 1024ull * 1024ull;
    if (EndsWithI(file, L".exe")) return 8ull * 1024ull * 1024ull;
    if (EndsWithI(file, L".msi")) return 12ull * 1024ull * 1024ull;

    return 1024ull * 1024ull;
}

static void DownloadWorker(std::vector<DownloadItem> items)
{
    DebugLogSeparator(L"download batch worker start");
    DebugLogLine(L"download", L"worker start items=" + std::to_wstring(items.size()) + L" log=" + GetHolyCheckDebugLogPath());
    g_working = true;

    std::stable_sort(items.begin(), items.end(), [](const DownloadItem& a, const DownloadItem& b)
    {
        unsigned long long as = GetEstimatedDownloadOrderBytes(a);
        unsigned long long bs = GetEstimatedDownloadOrderBytes(b);

        if (as != bs)
            return as < bs;

        if (a.tab != b.tab)
            return a.tab < b.tab;

        return ToLower(a.name) < ToLower(b.name);
    });

    std::wstring folder = GetHolyCheckFolder();

    if (!EnsureDirectory(folder))
    {
        DebugLogLine(L"download", L"worker failed ensure holycheck folder=" + folder);
        PostLog(L"Не создана папка holycheck");
        PostProgress(0);

        if (g_hMain && !g_destroying.load())
            PostMessageW(g_hMain, WM_APP_DONE, 0, 0);
        else
            g_working = false;

        return;
    }

    for (const auto& item : items)
    {
        if (!EnsureDirectory(CombinePath(folder, GetTabFolderName(item.tab))))
            PostLog(L"Не создана папка: " + GetTabFolderName(item.tab));
    }

    std::atomic<int> done = 0;
    std::mutex resultMutex;
    std::vector<std::wstring> downloadedItems;
    std::vector<std::wstring> failedItems;

    int totalOriginal = static_cast<int>(items.size());

    auto runDownloadBatch = [&](const std::vector<DownloadItem>& batch)
    {
        if (batch.empty() || IsCancelRequested())
            return;

        std::atomic<int> batchIndex = 0;
        int remaining = static_cast<int>(batch.size());
        int workers = static_cast<int>(std::thread::hardware_concurrency());

        if (workers < 4) workers = 4;
        // Раньше было максимум 4 потока. Это не ограничение скорости одного файла,
        // но ограничивало параллельную загрузку. 8 обычно безопаснее для сети/серверов,
        // чем десятки одновременных подключений.
        if (workers > 8) workers = 8;

        if (workers > remaining) workers = remaining;
        if (workers < 1) workers = remaining > 0 ? 1 : 0;

        std::vector<std::thread> threads;

        for (int i = 0; i < workers; ++i)
        {
            threads.emplace_back([&]()
            {
                for (;;)
                {
                    int n = batchIndex.fetch_add(1);

                    if (n >= remaining || IsCancelRequested())
                        break;

                    bool ok = ProcessItem(batch[n], folder, true);

                    {
                        std::lock_guard<std::mutex> resultLock(resultMutex);

                        if (ok)
                            downloadedItems.push_back(batch[n].name);
                        else
                            failedItems.push_back(batch[n].name);
                    }

                    int currentDone = done.fetch_add(1) + 1;
                    int percent = totalOriginal > 0 ? currentDone * 100 / totalOriginal : 100;

                    PostProgress(percent);
                }
            });
        }

        for (auto& t : threads)
        {
            if (t.joinable())
                t.join();
        }
    };

    runDownloadBatch(items);

    DebugLogLine(L"download", L"worker downloads done downloaded=" + std::to_wstring(downloadedItems.size()) +
        L" failed=" + std::to_wstring(failedItems.size()) + L" cancelled=" + std::to_wstring(IsCancelRequested() ? 1 : 0));

    CleanupXmlFiles(folder);

    ClearActiveDownloads();
    InvalidateBottomPanel();

    if (!IsCancelRequested())
        PostDownloadSummary(downloadedItems, failedItems);

    if (IsCancelRequested())
    {
        PostProgress(0);
    }
    else
    {
        PostProgress(100);
    }

    HWND hwnd = g_hMain;

    if (hwnd && !g_destroying.load())
        PostMessageW(hwnd, WM_APP_DONE, 0, 0);
    else
        g_working = false;
}

static void SetDownloadControlsEnabled(bool enabled)
{
    if (g_hDownloadAll)
    {
        EnableWindow(g_hDownloadAll, enabled ? TRUE : FALSE);
        InvalidateRect(g_hDownloadAll, nullptr, FALSE);
    }


    if (g_hDeleteHolyCheck)
    {
        EnableWindow(g_hDeleteHolyCheck, enabled ? TRUE : FALSE);
        InvalidateRect(g_hDeleteHolyCheck, nullptr, FALSE);
    }

    if (g_hCancelDownload)
        InvalidateRect(g_hCancelDownload, nullptr, FALSE);

    for (HWND button : g_tileButtons)
    {
        EnableWindow(button, enabled ? TRUE : FALSE);
        InvalidateRect(button, nullptr, FALSE);
    }
}

static void StartDownload(const std::vector<DownloadItem>& items)
{
    DebugLogLine(L"download", L"start download request items=" + std::to_wstring(items.size()) + L" working=" + std::to_wstring(g_working.load() ? 1 : 0));
    if (items.empty())
        return;

    if (g_working)
        return;

    if (g_activeSingleDownloads.load() > 0)
    {
        PostLog(L"Сейчас идут одиночные загрузки");
        return;
    }

    g_cancelRequested = false;
    AbortActiveHttpHandles();
    g_working = true;
    SetDownloadControlsEnabled(false);
    g_progressPercent = 0;
    ClearDownloadLog();
    PostLog(L"лог: " + GetHolyCheckDebugLogPath());
    ClearActiveDownloads();
    InvalidateBottomPanel();

    std::thread(DownloadWorker, items).detach();
}

static void DownloadCurrentTab()
{
    std::vector<DownloadItem> selected;

    for (const auto& item : g_items)
    {
        if (item.tab == g_currentTab)
            selected.push_back(item);
    }

    StartDownload(selected);
}


static std::wstring FindQuickLaunchPathForItemNoScan(const DownloadItem& item, const std::wstring& baseFolder)
{
    if (IsSystemInformerItem(item))
    {
        std::wstring installedPath = FindInstalledSystemInformerPath();
        if (!installedPath.empty())
            return installedPath;
    }

    std::wstring target = GetItemTargetPath(item, baseFolder);

    if (EndsWithI(target, L".zip"))
        return L"";

    if (FileExists(target))
        return target;

    return L"";
}

static void OpenOrDownloadSingleWorker(int itemIndex, DownloadItem item)
{
    DebugLogSeparator(L"single download worker start");
    DebugLogLine(L"download", L"single worker start index=" + std::to_wstring(itemIndex) + L" item=" + item.name + L" log=" + GetHolyCheckDebugLogPath());
    std::wstring folder = GetHolyCheckFolder();
    bool foldersOk = EnsureDirectory(folder);

    std::wstring tabFolder = CombinePath(folder, GetTabFolderName(item.tab));
    foldersOk = foldersOk && EnsureDirectory(tabFolder);

    bool processed = foldersOk && ProcessItem(item, folder, true);
    DebugLogLine(L"download", L"single worker processed item=" + item.name + L" foldersOk=" + std::to_wstring(foldersOk ? 1 : 0) +
        L" processed=" + std::to_wstring(processed ? 1 : 0));

    if (processed && !IsCancelRequested())
    {
        std::wstring launchPath = FindLaunchPathForItem(item, folder);

        if (OpenItemPath(item, launchPath))
        {
            PostLog(L"Открыто: " + item.name);
        }
        else
        {
            PostLog(L"Не открылось: " + item.name);
        }
    }
    else if (IsCancelRequested())
    {
        PostLog(L"Отменено: " + item.name);
    }
    else
    {
        PostLog(L"Не удалось: " + item.name);
    }

    int doneNow = g_singleDone.fetch_add(1) + 1;
    int totalNow = std::max(g_singleTotal.load(), doneNow);
    int percent = totalNow > 0 ? doneNow * 100 / totalNow : 100;
    PostProgress(percent);

    if (g_hMain && !g_destroying.load())
        PostMessageW(g_hMain, WM_APP_ITEM_DONE, static_cast<WPARAM>(itemIndex), 0);

    if (g_activeSingleDownloads.fetch_sub(1) == 1)
    {
        ClearActiveDownloads();
        InvalidateBottomPanel();

        if (IsCancelRequested())
        {
            PostProgress(0);
        }
        else
        {
            PostProgress(100);
        }

        g_cancelRequested = false;
    }
}

static void OpenOrDownloadSingleById(int id)
{
    DebugLogLine(L"download", L"single request id=" + std::to_wstring(id));
    int index = id - IDC_TILE_BASE;

    if (index < 0 || index >= static_cast<int>(g_items.size()))
        return;

    if (g_working || IsCancelRequested() || IsItemBusyIndex(index))
        return;

    const DownloadItem& item = g_items[index];
    std::wstring folder = GetHolyCheckFolder();
    std::wstring tabFolder = CombinePath(folder, GetTabFolderName(item.tab));

    if (!EnsureDirectory(tabFolder))
    {
        PostLog(L"Не создана папка: " + GetTabFolderName(item.tab));
        return;
    }

    std::wstring quickLaunchPath = FindQuickLaunchPathForItemNoScan(item, folder);

    if (!quickLaunchPath.empty() && FileExists(quickLaunchPath))
    {
        DebugLogLine(L"download", L"single quick launch item=" + item.name + L" path=" + quickLaunchPath);
        if (item.tab == 0)
            CleanupOldJuniorTextFiles(tabFolder);

        OpenItemPath(item, quickLaunchPath);
        return;
    }

    int previousActive = g_activeSingleDownloads.fetch_add(1);

    if (previousActive == 0)
    {
        g_cancelRequested = false;
        AbortActiveHttpHandles();
        g_singleTotal = 0;
        g_singleDone = 0;
        g_progressPercent = 0;
        ClearDownloadLog();
        PostLog(L"лог: " + GetHolyCheckDebugLogPath());
        ClearActiveDownloads();
    }

    g_singleTotal.fetch_add(1);
    SetItemBusyIndex(index, true);
    SetTileEnabledByIndex(index, false);

    AddActiveDownload(item.name);
    UpdateActiveDownloadStatus(item.name, L"старт", 1, 1, 0, 0);
    PostProgress(0);
    InvalidateBottomPanel();

    std::thread(OpenOrDownloadSingleWorker, index, item).detach();
}

static void CloseProcessesFromFolder(const std::wstring& folder);

static bool DeleteFileOrFolderPermanent(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());

    if (attr == INVALID_FILE_ATTRIBUTES)
        return true;

    if (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
    {
        DWORD cleanAttr = attr & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        if (cleanAttr == 0)
            cleanAttr = FILE_ATTRIBUTE_NORMAL;

        SetFileAttributesW(path.c_str(), cleanAttr);
    }

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(path.c_str()) || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;

    std::wstring mask = CombinePath(path, L"*");

    WIN32_FIND_DATAW data{};
    HANDLE hFind = FindFirstFileW(mask.c_str(), &data);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;

            DeleteFileOrFolderPermanent(CombinePath(path, data.cFileName));
        }
        while (FindNextFileW(hFind, &data));

        FindClose(hFind);
    }

    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
}

static bool DeleteDirectoryTree(const std::wstring& folder)
{
    if (!IsSafeHolyCheckDeletionTarget(folder))
    {
        PostLog(L"Небезопасный путь удаления");
        return false;
    }

    if (!DirectoryExists(folder))
        return true;

    for (int attempt = 0; attempt < 6; ++attempt)
    {
        CloseProcessesFromFolder(folder);

        if (DeleteFileOrFolderPermanent(folder))
            return true;

        Sleep(450);
    }

    return !DirectoryExists(folder);
}

static bool RegistryGetString(HKEY key, const wchar_t* valueName, std::wstring& out)
{
    DWORD type = 0;
    DWORD cb = 0;

    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS)
        return false;

    if ((type != REG_SZ && type != REG_EXPAND_SZ) || cb == 0)
        return false;

    std::wstring value(cb / sizeof(wchar_t), L'\0');

    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&value[0]), &cb) != ERROR_SUCCESS)
        return false;

    while (!value.empty() && value.back() == L'\0')
        value.pop_back();

    if (type == REG_EXPAND_SZ)
    {
        wchar_t expanded[4096]{};

        if (ExpandEnvironmentStringsW(value.c_str(), expanded, ARRAYSIZE(expanded)) > 0)
            value = expanded;
    }

    out = value;
    return !out.empty();
}

static bool RunElevatedCommandAndWait(const std::wstring& command)
{
    if (command.empty())
        return false;

    std::wstring params = L"/c " + command;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei))
        return false;

    bool ok = true;

    if (sei.hProcess)
    {
        DWORD waitResult = WaitForSingleObject(sei.hProcess, 120000);
        DWORD exitCode = 1;

        if (waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess(sei.hProcess, &exitCode) || exitCode != 0)
            ok = false;

        CloseHandle(sei.hProcess);
    }

    return ok;
}

static std::wstring MakeUninstallCommand(std::wstring command, bool addNsisSilent)
{
    std::wstring lower = ToLower(command);

    if (lower.find(L"msiexec") != std::wstring::npos)
    {
        size_t pos = lower.find(L"/i");

        if (pos != std::wstring::npos)
            command.replace(pos, 2, L"/X");

        pos = ToLower(command).find(L"-i");

        if (pos != std::wstring::npos)
            command.replace(pos, 2, L"/X");

        if (ToLower(command).find(L"/qn") == std::wstring::npos)
            command += L" /qn";

        if (ToLower(command).find(L"/norestart") == std::wstring::npos)
            command += L" /norestart";
    }
    else if (addNsisSilent)
    {
        std::wstring updatedLower = ToLower(command);

        if (updatedLower.find(L" /s") == std::wstring::npos &&
            updatedLower.find(L" -s") == std::wstring::npos &&
            updatedLower.find(L"/silent") == std::wstring::npos &&
            updatedLower.find(L"/verysilent") == std::wstring::npos &&
            updatedLower.find(L"/quiet") == std::wstring::npos)
        {
            command += L" /S";
        }
    }

    return command;
}

static bool DisplayNameMatchesAny(const std::wstring& displayName, const std::vector<std::wstring>& needles)
{
    std::wstring lowerName = ToLower(displayName);

    for (const auto& needle : needles)
    {
        if (lowerName.find(ToLower(needle)) != std::wstring::npos)
            return true;
    }

    return false;
}

static bool FindUninstallCommandInRoot(
    HKEY root,
    REGSAM viewFlags,
    const std::vector<std::wstring>& displayNameNeedles,
    bool addNsisSilent,
    std::wstring& out)
{
    const wchar_t* uninstallPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    HKEY hUninstall = nullptr;

    if (RegOpenKeyExW(root, uninstallPath, 0, KEY_READ | viewFlags, &hUninstall) != ERROR_SUCCESS)
        return false;

    bool found = false;

    for (DWORD i = 0; !found; ++i)
    {
        wchar_t subKeyName[512]{};
        DWORD subKeyLen = ARRAYSIZE(subKeyName);

        if (RegEnumKeyExW(hUninstall, i, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        HKEY hApp = nullptr;

        if (RegOpenKeyExW(hUninstall, subKeyName, 0, KEY_READ | viewFlags, &hApp) != ERROR_SUCCESS)
            continue;

        std::wstring displayName;

        if (RegistryGetString(hApp, L"DisplayName", displayName) &&
            DisplayNameMatchesAny(displayName, displayNameNeedles))
        {
            std::wstring quietUninstall;
            std::wstring uninstall;

            if (RegistryGetString(hApp, L"QuietUninstallString", quietUninstall))
                out = MakeUninstallCommand(quietUninstall, addNsisSilent);
            else if (RegistryGetString(hApp, L"UninstallString", uninstall))
                out = MakeUninstallCommand(uninstall, addNsisSilent);

            found = !out.empty();
        }

        RegCloseKey(hApp);
    }

    RegCloseKey(hUninstall);
    return found;
}

static bool FindUninstallCommand(
    const std::vector<std::wstring>& displayNameNeedles,
    bool addNsisSilent,
    std::wstring& out)
{
    return
        FindUninstallCommandInRoot(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, displayNameNeedles, addNsisSilent, out) ||
        FindUninstallCommandInRoot(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, displayNameNeedles, addNsisSilent, out) ||
        FindUninstallCommandInRoot(HKEY_CURRENT_USER, 0, displayNameNeedles, addNsisSilent, out);
}

static bool UninstallProgramIfPresent(
    const std::wstring& logName,
    const std::vector<std::wstring>& displayNameNeedles,
    bool addNsisSilent)
{
    std::wstring command;

    if (!FindUninstallCommand(displayNameNeedles, addNsisSilent, command))
    {
        PostLog(logName + L": не установлен");
        return true;
    }

    PostLog(logName + L": удаление");

    if (!RunElevatedCommandAndWait(command))
    {
        PostLog(logName + L": ошибка удаления");
        return false;
    }

    PostLog(logName + L": команда выполнена");
    return true;
}

static bool UninstallSystemInformerIfPresent()
{
    return UninstallProgramIfPresent(
        L"SystemInformer",
        { L"system informer", L"systeminformer" },
        false);
}

static bool UninstallRecuvaIfPresent()
{
    return UninstallProgramIfPresent(
        L"Recuva",
        { L"recuva", L"piriform recuva", L"ccleaner recuva" },
        true);
}

static bool IsPathInsideFolder(const std::wstring& path, const std::wstring& folder)
{
    std::wstring p = ToLower(path);
    std::wstring f = ToLower(folder);

    while (!f.empty() && (f.back() == L'\\' || f.back() == L'/'))
        f.pop_back();

    if (p.size() <= f.size())
        return false;

    if (p.compare(0, f.size(), f) != 0)
        return false;

    return p[f.size()] == L'\\' || p[f.size()] == L'/';
}


static bool IsProcessNameEqual(const wchar_t* processName, const wchar_t* wantedName)
{
    if (!processName || !wantedName)
        return false;
    return _wcsicmp(processName, wantedName) == 0;
}

static void CloseProcessesByName(const std::vector<std::wstring>& names)
{
    if (names.empty())
        return;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    DWORD currentPid = GetCurrentProcessId();
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            if (pe.th32ProcessID == currentPid)
                continue;

            bool matched = false;
            for (const std::wstring& name : names)
            {
                if (IsProcessNameEqual(pe.szExeFile, name.c_str()))
                {
                    matched = true;
                    break;
                }
            }

            if (!matched)
                continue;

            HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            if (!process)
                continue;

            PostLog(L"Закрывается: " + std::wstring(pe.szExeFile));
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 3000);
            CloseHandle(process);
        }
        while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
}

static void CloseProcessesFromFolder(const std::wstring& folder)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    DWORD currentPid = GetCurrentProcessId();
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            if (pe.th32ProcessID == currentPid)
                continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);

            if (!process)
                continue;

            wchar_t imagePath[MAX_PATH * 4]{};
            DWORD size = ARRAYSIZE(imagePath);

            if (QueryFullProcessImageNameW(process, 0, imagePath, &size))
            {
                if (IsPathInsideFolder(imagePath, folder))
                {
                    PostLog(L"Закрывается: " + std::wstring(pe.szExeFile));
                    TerminateProcess(process, 0);
                    WaitForSingleObject(process, 3000);
                }
            }

            CloseHandle(process);
        }
        while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
}


static std::wstring GetHolyCheckerLastCheckDirMarkerPath()
{
    wchar_t tempPath[MAX_PATH * 4]{};
    DWORD len = GetTempPathW(ARRAYSIZE(tempPath), tempPath);

    if (len == 0 || len >= ARRAYSIZE(tempPath))
        return L"";

    return CombinePath(tempPath, L"HolyChecker_last_check_dir.txt");
}

static std::wstring TrimText(std::wstring text)
{
    while (!text.empty() && (text.front() == static_cast<wchar_t>(0xFEFF) || iswspace(text.front())))
        text.erase(text.begin());

    while (!text.empty() && iswspace(text.back()))
        text.pop_back();

    return text;
}

static std::wstring ReadSmallUtf8TextFile(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return L"";

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32768)
    {
        CloseHandle(file);
        return L"";
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);

    if (!ok || read == 0)
        return L"";

    bytes.resize(read);

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLen <= 0)
        return L"";

    std::wstring text(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), &text[0], wideLen);
    return TrimText(text);
}

static bool IsSafeCheckOutputFolder(const std::wstring& folder)
{
    if (folder.empty() || !DirectoryExists(folder))
        return false;

    std::wstring profile = GetCurrentUserProfilePath();
    std::wstring holyCheck = GetHolyCheckFolderPathOnly();

    if (!profile.empty() && IsPathUnderFolder(folder, profile))
        return true;

    if (!holyCheck.empty() && IsPathUnderFolder(folder, holyCheck))
        return true;

    if (IsPathUnderFolder(folder, L"C:\\PulseVisuals\\game"))
        return true;

    return false;
}

static void DeleteCheckOutputFilesInFolder(const std::wstring& folder, int& deletedCount)
{
    if (!IsSafeCheckOutputFolder(folder))
        return;

    const wchar_t* names[] = { L"check.txt", L"checktwinks.txt" };

    for (const wchar_t* name : names)
    {
        std::wstring path = CombinePath(folder, name);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;

        if (DeleteFileOrFolderPermanent(path))
            ++deletedCount;
    }
}

static void DeletePowerShellCheckOutputFiles()
{
    std::vector<std::wstring> folders;

    std::wstring marker = GetHolyCheckerLastCheckDirMarkerPath();
    if (!marker.empty())
    {
        std::wstring lastFolder = ReadSmallUtf8TextFile(marker);
        if (!lastFolder.empty())
            AddUniquePath(folders, lastFolder);
    }

    AddUniquePath(folders, GetHolyCheckFolderPathOnly());

    std::wstring profile = GetCurrentUserProfilePath();
    if (!profile.empty())
    {
        AddUniquePath(folders, CombinePath(CombinePath(profile, L"AppData\\Roaming"), L".minecraft"));
        AddUniquePath(folders, CombinePath(CombinePath(profile, L"AppData\\Roaming"), L".tlauncher\\legacy\\Minecraft\\game"));
        AddUniquePath(folders, CombinePath(profile, L"Desktop"));
        AddUniquePath(folders, CombinePath(profile, L"Downloads"));
    }

    AddUniquePath(folders, L"C:\\PulseVisuals\\game");

    int deletedCount = 0;
    for (const std::wstring& folder : folders)
        DeleteCheckOutputFilesInFolder(folder, deletedCount);

    if (!marker.empty())
        DeleteFileW(marker.c_str());

    if (deletedCount > 0)
        PostLog(L"Удалены check.txt/checktwinks.txt: " + std::to_wstring(deletedCount));
}


static bool IsHolyCheckReleaseZipName(const std::wstring& fileName)
{
    std::wstring lower = ToLower(fileName);
    const std::wstring prefix = L"holycheck-v";
    const std::wstring suffix = L".zip";

    if (lower.size() <= prefix.size() + suffix.size())
        return false;

    if (lower.compare(0, prefix.size(), prefix) != 0)
        return false;

    if (lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;

    std::wstring version = lower.substr(prefix.size(), lower.size() - prefix.size() - suffix.size());
    if (version.empty() || version.front() == L'.' || version.back() == L'.')
        return false;

    bool hasDigit = false;
    bool previousDot = false;

    for (wchar_t ch : version)
    {
        if (ch >= L'0' && ch <= L'9')
        {
            hasDigit = true;
            previousDot = false;
            continue;
        }

        if (ch == L'.')
        {
            if (previousDot)
                return false;
            previousDot = true;
            continue;
        }

        return false;
    }

    return hasDigit;
}

static std::wstring GetKnownFolderPathSafe(REFKNOWNFOLDERID folderId)
{
    PWSTR raw = nullptr;
    std::wstring result;

    if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &raw)) && raw)
    {
        result = raw;
        CoTaskMemFree(raw);
    }

    return result;
}

static bool IsCurrentUserDesktopOrDownloads(const std::wstring& folder)
{
    std::wstring full = ToLower(GetFullPathSafe(folder));
    if (full.empty())
        return false;

    std::wstring downloads = ToLower(GetFullPathSafe(GetKnownFolderPathSafe(FOLDERID_Downloads)));
    std::wstring desktop = ToLower(GetFullPathSafe(GetKnownFolderPathSafe(FOLDERID_Desktop)));

    return (!downloads.empty() && full == downloads) || (!desktop.empty() && full == desktop);
}

static void DeleteHolyCheckReleaseZipsInFolder(const std::wstring& folder, int& deletedCount)
{
    if (!IsCurrentUserDesktopOrDownloads(folder) || !DirectoryExists(folder))
        return;

    std::wstring mask = CombinePath(folder, L"HolyCheck-v*.zip");
    WIN32_FIND_DATAW data{};
    HANDLE hFind = FindFirstFileW(mask.c_str(), &data);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        if (!IsHolyCheckReleaseZipName(data.cFileName))
            continue;

        std::wstring path = CombinePath(folder, data.cFileName);
        DebugLogLine(L"cleanup", L"delete HolyCheck release zip candidate: " + path);

        if (DeleteFileOrFolderPermanent(path))
            ++deletedCount;
    }
    while (FindNextFileW(hFind, &data));

    FindClose(hFind);
}

static void DeleteHolyCheckReleaseZipsInKnownFolders()
{
    int deletedCount = 0;

    DeleteHolyCheckReleaseZipsInFolder(GetKnownFolderPathSafe(FOLDERID_Downloads), deletedCount);
    DeleteHolyCheckReleaseZipsInFolder(GetKnownFolderPathSafe(FOLDERID_Desktop), deletedCount);

    if (deletedCount > 0)
        PostLog(L"Удалены архивы HolyCheck-v*.zip: " + std::to_wstring(deletedCount));
}


static std::wstring QuoteCmdArgument(const std::wstring& value)
{
    std::wstring out = L"\"";
    for (wchar_t ch : value)
    {
        if (ch == L'\"')
            out += L"\\\"";
        else
            out += ch;
    }
    out += L"\"";
    return out;
}

static bool IsHolyCheckLikeFolderName(const std::wstring& folder)
{
    std::wstring full = GetFullPathSafe(folder);
    size_t slash = full.find_last_of(L"\\/");
    std::wstring leaf = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
    std::wstring lower = ToLower(leaf);
    return lower == L"holycheck" || lower == L"holycxheker" || lower.find(L"holycheck") != std::wstring::npos;
}

static bool WriteUtf8TextFileLocal(const std::wstring& path, const std::wstring& text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    std::string bytes;
    if (!text.empty())
    {
        int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (need > 1)
        {
            bytes.resize(static_cast<size_t>(need - 1));
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, bytes.data(), need, nullptr, nullptr);
        }
    }

    DWORD written = 0;
    BOOL ok = TRUE;
    if (!bytes.empty())
        ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

static std::wstring BatchSetValue(std::wstring value)
{
    for (wchar_t& ch : value)
    {
        if (ch == L'"')
            ch = L'\'';
    }
    return value;
}

static std::wstring EscapePowerShellSingleQuotedLocal(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size() + 8);
    for (wchar_t ch : value)
    {
        if (ch == L'\'')
            out += L"''";
        else
            out += ch;
    }
    return out;
}

static void ScheduleSelfDeleteAndCloseProgram()
{
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath)))
        return;

    std::wstring exe = exePath;
    std::wstring folder;
    size_t slash = exe.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        folder = exe.substr(0, slash);

    wchar_t tempDir[MAX_PATH]{};
    std::wstring workDir = L"C:\\";
    DWORD tempLen = GetTempPathW(ARRAYSIZE(tempDir), tempDir);
    if (tempLen > 0 && tempLen < ARRAYSIZE(tempDir))
        workDir = tempDir;
    while (!workDir.empty() && (workDir.back() == L'\\' || workDir.back() == L'/'))
        workDir.pop_back();

    // Старая логика удаления папки выполняется в DeleteHolyCheckWorker через
    // DeleteDirectoryTree(folder), пока программа ещё открыта. Этот helper нужен
    // только для последнего заблокированного файла — текущего HolyCheck.exe — и
    // финального удаления уже пустой папки после закрытия процесса.
    std::wstring cmd = L"/C ping 127.0.0.1 -n 3 >nul";
    cmd += L" & attrib -r -s -h " + QuoteCmdArgument(exe) + L" >nul 2>nul";
    cmd += L" & del /f /q " + QuoteCmdArgument(exe) + L" >nul 2>nul";
    if (!folder.empty())
    {
        // Повторяем коротко, чтобы успеть после полного завершения процесса.
        cmd += L" & for /l %i in (1,1,20) do @if exist " + QuoteCmdArgument(folder) +
               L" (rmdir " + QuoteCmdArgument(folder) + L" >nul 2>nul & if exist " +
               QuoteCmdArgument(folder) + L" ping 127.0.0.1 -n 2 >nul)";
    }

    wchar_t sysDir[MAX_PATH]{};
    std::wstring cmdExe = L"cmd.exe";
    if (GetSystemDirectoryW(sysDir, ARRAYSIZE(sysDir)))
        cmdExe = CombinePath(sysDir, L"cmd.exe");

    std::wstring commandLine = QuoteCmdArgument(cmdExe) + L" " + cmd;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    std::vector<wchar_t> cwd(workDir.begin(), workDir.end());
    cwd.push_back(L'\0');

    if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.data(), &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}


static void DeleteHolyCheckWorker()
{
    RequestCancelDownloads();

    AbortActiveHttpHandles();

    for (int i = 0; i < 10 && (g_working.load() || g_activeSingleDownloads.load() > 0); ++i)
        Sleep(50);

    std::wstring folder = GetHolyCheckFolderPathOnly();

    DeletePowerShellCheckOutputFiles();
    DeleteHolyCheckReleaseZipsInKnownFolders();

    PostLog(L"Закрываю программы");
    CloseProcessesByName({ L"powershell.exe", L"pwsh.exe" });
    CloseProcessesFromFolder(folder);

    UninstallSystemInformerIfPresent();
    UninstallRecuvaIfPresent();

    CloseProcessesFromFolder(folder);

    // Возвращена старая логика: сначала удаляется папка HolyCheck тем же
    // DeleteDirectoryTree(folder). Текущий exe может остаться заблокированным,
    // поэтому после закрытия программы helper удалит только exe и пустую папку.
    if (DeleteDirectoryTree(folder))
        PostLog(L"Папка удалена полностью");
    else
        PostLog(L"Папка удалена частично, завершаю HolyCheck и удаляю exe");

    ClearActiveDownloads();
    g_deleteInProgress = false;
    PostProgress(0);

    ScheduleSelfDeleteAndCloseProgram();

    if (g_hMain && !g_destroying.load())
        PostMessageW(g_hMain, WM_CLOSE, 0, 0);

    Sleep(300);
    ExitProcess(0);
}


static void DeleteHolyCheckFolder()
{
    if (g_deleteInProgress.exchange(true))
        return;

    g_cancelRequested = true;
    SetDownloadControlsEnabled(false);
    g_progressPercent = 0;
    ClearDownloadLog();
    PostLog(L"Удаление holycheck");
    InvalidateBottomPanel();

    std::thread(DeleteHolyCheckWorker).detach();
}

