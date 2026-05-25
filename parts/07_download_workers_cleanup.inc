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
    if (IsCancelRequested())
        return false;

    std::wstring tabFolder = CombinePath(baseFolder, GetTabFolderName(item.tab));

    if (!EnsureDirectory(tabFolder))
    {
        return false;
    }

    if (item.tab == 0)
        CleanupOldJuniorTextFiles(tabFolder);

    std::wstring target = CombinePath(tabFolder, item.file);
    bool zipItem = EndsWithI(target, L".zip");
    bool hadFileBefore = FileExists(target);
    bool downloadedThisRun = false;

    if (hadFileBefore && !ValidateDownloadedFileOrDelete(item, target))
        hadFileBefore = false;

    if (!hadFileBefore)
    {
        AddActiveDownload(item.name);

        bool downloaded = DownloadPrimaryUrlOnlyValidated(item, target);

        RemoveActiveDownload(item.name);

        if (!downloaded)
        {
            return false;
        }

        downloadedThisRun = true;
    }

    if (IsCancelRequested())
        return false;

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

            if (!ExtractZip(target, extractDir))
            {
                return false;
            }

            if (!WaitForExtractedExe(extractDir, exePath))
            {
                return false;
            }

            if (!ShouldSkipXmlCleanupForItem(item))
                CleanupXmlFiles(extractDir);

            WriteExtractionMarker(extractDir);
        }
    }

    if (!ShouldSkipXmlCleanupForItem(item))
        CleanupXmlFiles(tabFolder);

    return FileExists(target) && !IsCancelRequested();
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

static void DownloadWorker(std::vector<DownloadItem> items)
{
    g_working = true;

    std::stable_sort(items.begin(), items.end(), [](const DownloadItem& a, const DownloadItem& b)
    {
        if (a.tab != b.tab)
            return a.tab < b.tab;

        bool aPowerShellFirst = ShouldTryPowerShellBeforeCurl(a);
        bool bPowerShellFirst = ShouldTryPowerShellBeforeCurl(b);

        if (aPowerShellFirst != bPowerShellFirst)
            return aPowerShellFirst;

        return !IsFastFailItem(a) && IsFastFailItem(b);
    });

    std::wstring folder = GetHolyCheckFolder();

    if (!EnsureDirectory(folder))
    {
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

        if (workers < 2) workers = 2;
        if (workers > 4) workers = 4;

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

    for (int tab = 0; tab < TAB_COUNT && !IsCancelRequested(); ++tab)
    {
        std::vector<DownloadItem> powerShellFirstBatch;
        std::vector<DownloadItem> curlFirstBatch;

        for (const auto& item : items)
        {
            if (item.tab != tab)
                continue;

            if (ShouldTryPowerShellBeforeCurl(item))
                powerShellFirstBatch.push_back(item);
            else
                curlFirstBatch.push_back(item);
        }

        runDownloadBatch(powerShellFirstBatch);
        runDownloadBatch(curlFirstBatch);
    }

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

static void OpenOrDownloadSingleWorker(int itemIndex, DownloadItem item)
{
    std::wstring folder = GetHolyCheckFolder();
    bool foldersOk = EnsureDirectory(folder);

    std::wstring tabFolder = CombinePath(folder, GetTabFolderName(item.tab));
    foldersOk = foldersOk && EnsureDirectory(tabFolder);

    bool processed = foldersOk && ProcessItem(item, folder, true);

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

    std::wstring quickLaunchPath = FindLaunchPathForItem(item, folder);

    if (!quickLaunchPath.empty() && FileExists(quickLaunchPath))
    {
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
        ClearActiveDownloads();
    }

    g_singleTotal.fetch_add(1);
    SetItemBusyIndex(index, true);
    SetTileEnabledByIndex(index, false);
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

static void DeleteHolyCheckWorker()
{
    RequestCancelDownloads();

    AbortActiveHttpHandles();

    for (int i = 0; i < 35 && (g_working.load() || g_activeSingleDownloads.load() > 0); ++i)
        Sleep(100);

    std::wstring folder = GetHolyCheckFolderPathOnly();

    PostLog(L"Закрываю программы");
    CloseProcessesFromFolder(folder);

    UninstallSystemInformerIfPresent();
    UninstallRecuvaIfPresent();

    CloseProcessesFromFolder(folder);

    if (DeleteDirectoryTree(folder))
        PostLog(L"Папка удалена полностью");
    else
        PostLog(L"Ошибка удаления папки");

    ClearActiveDownloads();
    g_deleteInProgress = false;
    PostProgress(0);

    if (g_hMain && !g_destroying.load())
        PostMessageW(g_hMain, WM_APP_DONE, 0, 0);
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

