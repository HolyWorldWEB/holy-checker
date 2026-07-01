static const unsigned long long MIN_ACCEPTABLE_DOWNLOAD_SPEED_BPS = 14ull * 1024ull;
static const unsigned long long FAST_SWITCH_DOWNLOAD_SPEED_BPS = 96ull * 1024ull;

static bool ReadFilePrefix(const std::wstring& path, std::vector<unsigned char>& out, DWORD maxBytes);
static bool StartsWithBytes(const std::vector<unsigned char>& data, std::initializer_list<unsigned char> bytes);
static bool LooksLikeDownloadedHtml(const std::vector<unsigned char>& data);
static bool IsZipArchiveStructurallyValid(const std::wstring& path);

static unsigned long long GetMinCompleteExeSizeForName(const std::wstring& itemName)
{
    std::wstring lower = ToLower(itemName);

    if (lower.find(L"systeminformer") != std::wstring::npos ||
        (lower.find(L"system") != std::wstring::npos && lower.find(L"informer") != std::wstring::npos))
        return 10ull * 1024ull * 1024ull;

    if (lower.find(L"processhacker") != std::wstring::npos ||
        (lower.find(L"process") != std::wstring::npos && lower.find(L"hacker") != std::wstring::npos))
        return 1024ull * 1024ull;

    if (lower.find(L"bamparser") != std::wstring::npos || lower.find(L"bam parser") != std::wstring::npos)
        return 200ull * 1024ull;

    if (lower.find(L"journaltrace") != std::wstring::npos ||
        (lower.find(L"journal") != std::wstring::npos && lower.find(L"trace") != std::wstring::npos))
        return 100ull * 1024ull;

    if (lower.find(L"recuva") != std::wstring::npos)
        return 2ull * 1024ull * 1024ull;

    return 4096ull;
}

static bool TryFinalizeAbortedPartFile(
    const std::wstring& itemName,
    const std::wstring& partPath,
    const std::wstring& targetPath)
{
    if (!FileExists(partPath) || IsCancelRequested())
        return false;

    unsigned long long partSize = 0;
    if (!GetFileSizeBytes(partPath, partSize) || partSize == 0)
        return false;

    std::vector<unsigned char> head;
    if (!ReadFilePrefix(partPath, head, 32) || LooksLikeDownloadedHtml(head))
        return false;

    std::wstring lowerTarget = ToLower(targetPath);

    if (EndsWithI(lowerTarget, L".exe") || EndsWithI(lowerTarget, L".msi"))
    {
        return false;
    }
    else if (EndsWithI(lowerTarget, L".zip") || EndsWithI(lowerTarget, L".jar"))
    {
        if (!StartsWithBytes(head, { 0x50, 0x4B }) || !IsZipArchiveStructurallyValid(partPath))
            return false;
    }
    else if (EndsWithI(lowerTarget, L".rar"))
    {
        if (!StartsWithBytes(head, { 0x52, 0x61, 0x72, 0x21 }))
            return false;
    }
    else if (EndsWithI(lowerTarget, L".7z"))
    {
        if (!StartsWithBytes(head, { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C }))
            return false;
    }

    DeleteFileW(targetPath.c_str());
    if (!MoveFileExW(partPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING))
        return false;

    return true;
}

static bool IsFastSpeedSwitchItem(const DownloadItem& item)
{
    std::wstring lower = ToLower(item.name);

    return IsSystemInformerItem(item) ||
        IsProcessHackerItem(item) ||
        IsBamParserItem(item) ||
        IsJumpListExplorerItem(item) ||
        IsRegistryExplorerItem(item) ||
        IsSimpleUnlockerItem(item) ||
        lower.find(L"journaltrace") != std::wstring::npos ||
        (lower.find(L"journal") != std::wstring::npos && lower.find(L"trace") != std::wstring::npos);
}

static bool IsProblemDownloadName(const std::wstring& name)
{
    std::wstring lower = ToLower(name);

    return lower.find(L"systeminformer") != std::wstring::npos ||
        (lower.find(L"system") != std::wstring::npos && lower.find(L"informer") != std::wstring::npos) ||
        lower.find(L"processhacker") != std::wstring::npos ||
        (lower.find(L"process") != std::wstring::npos && lower.find(L"hacker") != std::wstring::npos) ||
        lower.find(L"bamparser") != std::wstring::npos ||
        lower.find(L"bam parser") != std::wstring::npos ||
        lower.find(L"registryexplorer") != std::wstring::npos ||
        (lower.find(L"registry") != std::wstring::npos && lower.find(L"explorer") != std::wstring::npos) ||
        lower.find(L"jumplistexplorer") != std::wstring::npos ||
        (lower.find(L"jumplist") != std::wstring::npos && lower.find(L"explorer") != std::wstring::npos) ||
        lower.find(L"simpleunlocker") != std::wstring::npos ||
        (lower.find(L"simple") != std::wstring::npos && lower.find(L"unlocker") != std::wstring::npos) ||
        lower.find(L"journaltrace") != std::wstring::npos ||
        (lower.find(L"journal") != std::wstring::npos && lower.find(L"trace") != std::wstring::npos);
}

static bool IsProblemDownloadItem(const DownloadItem& item)
{
    return IsProblemDownloadName(item.name);
}

static bool ShouldUseWinHttpFirst(const DownloadItem& item)
{
    return IsProblemDownloadItem(item);
}

static unsigned long long GetMethodMinSpeedBps(const DownloadItem& item)
{
    if (IsFastSpeedSwitchItem(item))
        return FAST_SWITCH_DOWNLOAD_SPEED_BPS; // меньше ~96 КБ/сек -> другой метод/ссылка

    return MIN_ACCEPTABLE_DOWNLOAD_SPEED_BPS;
}

static DWORD GetMethodLowSpeedWindowMs(const DownloadItem& item)
{
    if (IsFastSpeedSwitchItem(item))
        return 2500;

    return 3000;
}

static DWORD GetMethodNoProgressAbortMs(const DownloadItem& item)
{
    if (IsFastSpeedSwitchItem(item))
        return 3000;

    return 3500;
}

static unsigned long long GetPowerShellMinSpeedBps(const DownloadItem& item)
{
    return GetMethodMinSpeedBps(item);
}

static DWORD GetPowerShellLowSpeedWindowMs(const DownloadItem& item)
{
    return GetMethodLowSpeedWindowMs(item);
}

static DWORD GetPowerShellNoProgressAbortMs(const DownloadItem& item)
{
    return GetMethodNoProgressAbortMs(item);
}

static std::wstring BuildCurlLowSpeedArgs(
    unsigned long long minBytesPerSecond,
    DWORD lowSpeedWindowMs)
{
    if (minBytesPerSecond == 0 || lowSpeedWindowMs == 0)
        return L"";

    DWORD seconds = std::max<DWORD>(2, lowSpeedWindowMs / 1000);
    return L" --speed-limit " + std::to_wstring(minBytesPerSecond) +
        L" --speed-time " + std::to_wstring(seconds);
}

static bool WaitForDownloadProcessWithInstantFallback(
    PROCESS_INFORMATION& pi,
    const std::wstring& partPath,
    DWORD timeoutMs,
    DWORD noProgressAbortMs,
    DWORD lowSpeedWindowMs,
    unsigned long long minBytesPerSecond,
    const std::wstring& itemName = L"",
    const std::wstring& methodName = L"",
    size_t linkIndex = 0,
    size_t linkTotal = 0)
{
    ULONGLONG started = GetTickCount64();
    ULONGLONG lastProgress = started;
    ULONGLONG lowWindowStarted = started;
    ULONGLONG speedWindowStarted = started;
    ULONGLONG lastUiUpdate = 0;
    unsigned long long lastSize = GetFileSizeBytesOrZero(partPath);
    unsigned long long lowWindowSize = lastSize;
    unsigned long long speedWindowSize = lastSize;
    g_lastDownloadSpeedBps = 0;
    DebugLogLine(L"download", L"wait process start item=" + itemName + L" method=" + methodName +
        L" link=" + std::to_wstring(linkIndex) + L"/" + std::to_wstring(linkTotal) +
        L" timeout=" + std::to_wstring(timeoutMs) +
        L" noProgress=" + std::to_wstring(noProgressAbortMs) +
        L" lowSpeedWindow=" + std::to_wstring(lowSpeedWindowMs) +
        L" minBps=" + std::to_wstring(minBytesPerSecond) +
        L" part=" + partPath);

    if (!itemName.empty())
        UpdateActiveDownloadStatus(itemName, methodName, linkIndex, linkTotal, lastSize, 0);

    auto abortProcess = [&](const std::wstring& reason) -> bool
    {
        MarkTransferStalled();
        DebugLogLine(L"download", L"wait process abort item=" + itemName + L" method=" + methodName +
            L" reason=" + reason + L" partBytes=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)) +
            L" speed=" + std::to_wstring(g_lastDownloadSpeedBps));
        if (!itemName.empty())
            UpdateActiveDownloadStatus(itemName, methodName, linkIndex, linkTotal, GetFileSizeBytesOrZero(partPath), g_lastDownloadSpeedBps);
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        return false;
    };

    for (;;)
    {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 250);
        ULONGLONG now = GetTickCount64();
        unsigned long long currentSize = GetFileSizeBytesOrZero(partPath);

        if (now - speedWindowStarted >= 1000)
        {
            unsigned long long delta = currentSize >= speedWindowSize ? currentSize - speedWindowSize : 0;
            g_lastDownloadSpeedBps = (delta * 1000ull) / std::max<ULONGLONG>(1, now - speedWindowStarted);
            speedWindowStarted = now;
            speedWindowSize = currentSize;
        }

        if (!itemName.empty() && now - lastUiUpdate >= 500)
        {
            UpdateActiveDownloadStatus(itemName, methodName, linkIndex, linkTotal, currentSize, g_lastDownloadSpeedBps);
            lastUiUpdate = now;
        }

        if (waitResult == WAIT_OBJECT_0)
        {
            if (!itemName.empty())
                UpdateActiveDownloadStatus(itemName, methodName, linkIndex, linkTotal, currentSize, g_lastDownloadSpeedBps);
            DebugLogLine(L"download", L"wait process finished item=" + itemName + L" method=" + methodName +
                L" bytes=" + std::to_wstring(currentSize) + L" speed=" + std::to_wstring(g_lastDownloadSpeedBps));
            return true;
        }

        if (waitResult == WAIT_FAILED)
            return abortProcess(L"WaitForSingleObject failed");

        if (IsCancelRequested())
            return abortProcess(L"cancel requested");

        if (now - started > timeoutMs)
            return abortProcess(L"timeout");

        if (currentSize > lastSize)
        {
            lastSize = currentSize;
            lastProgress = now;
        }

        if (noProgressAbortMs > 0 && now - lastProgress >= noProgressAbortMs)
            return abortProcess(L"no progress");

        if (lowSpeedWindowMs > 0 && minBytesPerSecond > 0)
        {
            ULONGLONG windowMs = now - lowWindowStarted;
            if (windowMs >= lowSpeedWindowMs)
            {
                unsigned long long delta = currentSize >= lowWindowSize ? currentSize - lowWindowSize : 0;
                unsigned long long bytesPerSecond = (delta * 1000ull) / std::max<ULONGLONG>(1, windowMs);

                if (bytesPerSecond < minBytesPerSecond)
                    return abortProcess(L"low speed " + std::to_wstring(bytesPerSecond) + L" < " + std::to_wstring(minBytesPerSecond));

                lowWindowStarted = now;
                lowWindowSize = currentSize;
            }
        }
    }
}

static bool WaitForProcessOrCancel(PROCESS_INFORMATION& pi, DWORD timeoutMs)
{
    ULONGLONG started = GetTickCount64();
    DebugLogLine(L"process", L"wait external process start timeout=" + std::to_wstring(timeoutMs));

    for (;;)
    {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 250);

        if (waitResult == WAIT_OBJECT_0)
        {
            DebugLogLine(L"process", L"wait external process done");
            return true;
        }

        ULONGLONG now = GetTickCount64();

        if (waitResult == WAIT_FAILED || IsCancelRequested() || now - started > timeoutMs)
        {
            MarkTransferStalled();
            DebugLogLine(L"process", L"wait external process abort waitResult=" + std::to_wstring(waitResult) +
                L" cancel=" + std::to_wstring(IsCancelRequested() ? 1 : 0) +
                L" elapsed=" + std::to_wstring(now - started));
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            return false;
        }
    }
}

static bool DownloadWithCurlFallback(
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    unsigned long long minBytesPerSecond,
    DWORD lowSpeedWindowMs,
    DWORD noProgressAbortMs,
    const std::wstring& itemName,
    size_t linkIndex,
    size_t linkTotal)
{
    ClearTransferStalledFlag();

    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";

    std::wstring curlExe = GetSystemToolPath(L"curl.exe");
    DWORD maxTimeSeconds = std::max<DWORD>(180, timeoutMs / 1000);
    DWORD connectTimeoutSeconds = 5;

    std::wstring command =
        QuoteProcessArgument(curlExe) +
        L" -L --fail --silent --show-error --ssl-no-revoke --tcp-nodelay"
        L" -C -"
        L" --retry 1 --retry-delay 1 --retry-connrefused" +
        BuildCurlLowSpeedArgs(minBytesPerSecond, lowSpeedWindowMs) +
        L" --connect-timeout " + std::to_wstring(connectTimeoutSeconds) +
        L" --max-time " + std::to_wstring(maxTimeSeconds) +
        L" -A " + QuoteProcessArgument(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36") +
        L" -H " + QuoteProcessArgument(L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,application/octet-stream,*/*;q=0.8") +
        L" -H " + QuoteProcessArgument(L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6") +
        L" -H " + QuoteProcessArgument(L"Accept-Encoding: identity") +
        L" -o " + QuoteProcessArgument(partPath) +
        L" " + QuoteProcessArgument(url);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    const wchar_t* appName = FileExists(curlExe) ? curlExe.c_str() : nullptr;

    if (!CreateProcessW(appName, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        DebugLogLine(L"download", L"CreateProcess failed command=" + command + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    if (!WaitForDownloadProcessWithInstantFallback(
        pi, partPath, timeoutMs,
        noProgressAbortMs, lowSpeedWindowMs, minBytesPerSecond,
        itemName, L"curl", linkIndex, linkTotal))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    DebugLogLine(L"download", L"process exit command=" + command + L" exit=" + std::to_wstring(exitCode) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode == 28)
        MarkTransferStalled();

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
    {
        DebugLogLine(L"download", L"curl MoveFile failed item=" + itemName + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    DebugLogLine(L"download", L"curl success item=" + itemName + L" bytes=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
    return true;
}


static bool DownloadWithCurlBrowserLike(
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    unsigned long long minBytesPerSecond,
    DWORD lowSpeedWindowMs,
    DWORD noProgressAbortMs,
    const std::wstring& itemName = L"",
    size_t linkIndex = 0,
    size_t linkTotal = 0)
{
    ClearTransferStalledFlag();

    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";

    std::wstring curlExe = GetSystemToolPath(L"curl.exe");
    DWORD maxTimeSeconds = std::max<DWORD>(300, timeoutMs / 1000);

    std::wstring command =
        QuoteProcessArgument(curlExe) +
        L" -L --fail --silent --show-error --ssl-no-revoke --tcp-nodelay"
        L" -C -"
        L" --retry 1 --retry-delay 1 --retry-connrefused" +
        BuildCurlLowSpeedArgs(minBytesPerSecond, lowSpeedWindowMs) +
        L" --connect-timeout 6"
        L" --max-time " + std::to_wstring(maxTimeSeconds) +
        L" -A " + QuoteProcessArgument(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36") +
        L" -H " + QuoteProcessArgument(L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,application/octet-stream,*/*;q=0.8") +
        L" -H " + QuoteProcessArgument(L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6") +
        L" -H " + QuoteProcessArgument(L"Accept-Encoding: identity") +
        L" -e " + QuoteProcessArgument(L"https://ericzimmerman.github.io/") +
        L" -o " + QuoteProcessArgument(partPath) +
        L" " + QuoteProcessArgument(url);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    const wchar_t* appName = FileExists(curlExe) ? curlExe.c_str() : nullptr;

    if (!CreateProcessW(appName, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        DebugLogLine(L"download", L"CreateProcess failed command=" + command + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    if (!WaitForDownloadProcessWithInstantFallback(
        pi, partPath, timeoutMs,
        noProgressAbortMs, lowSpeedWindowMs, minBytesPerSecond,
        itemName, L"curl", linkIndex, linkTotal))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    DebugLogLine(L"download", L"process exit command=" + command + L" exit=" + std::to_wstring(exitCode) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

    return true;
}


static bool DownloadWithCurlGitHubFast(
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    unsigned long long minBytesPerSecond,
    DWORD lowSpeedWindowMs,
    DWORD noProgressAbortMs,
    const std::wstring& itemName = L"",
    size_t linkIndex = 0,
    size_t linkTotal = 0)
{
    ClearTransferStalledFlag();

    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";

    std::wstring curlExe = GetSystemToolPath(L"curl.exe");
    DWORD maxTimeSeconds = std::max<DWORD>(600, timeoutMs / 1000);

    std::wstring command =
        QuoteProcessArgument(curlExe) +
        L" -L --fail --silent --show-error --ssl-no-revoke --tcp-nodelay"
        L" -C -"
        L" --retry 1 --retry-delay 1 --retry-connrefused" +
        BuildCurlLowSpeedArgs(minBytesPerSecond, lowSpeedWindowMs) +
        L" --connect-timeout 6"
        L" --max-time " + std::to_wstring(maxTimeSeconds) +
        L" -A " + QuoteProcessArgument(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36") +
        L" -H " + QuoteProcessArgument(L"Accept: application/octet-stream,application/x-msdownload,application/zip,*/*") +
        L" -H " + QuoteProcessArgument(L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6") +
        L" -H " + QuoteProcessArgument(L"Accept-Encoding: identity") +
        L" -e " + QuoteProcessArgument(L"https://github.com/") +
        L" -o " + QuoteProcessArgument(partPath) +
        L" " + QuoteProcessArgument(url);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    const wchar_t* appName = FileExists(curlExe) ? curlExe.c_str() : nullptr;

    if (!CreateProcessW(appName, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        DebugLogLine(L"download", L"CreateProcess failed command=" + command + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    if (!WaitForDownloadProcessWithInstantFallback(
        pi, partPath, timeoutMs,
        noProgressAbortMs, lowSpeedWindowMs, minBytesPerSecond,
        itemName, L"curl", linkIndex, linkTotal))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    DebugLogLine(L"download", L"process exit command=" + command + L" exit=" + std::to_wstring(exitCode) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode == 28)
        MarkTransferStalled();

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

    return true;
}

static bool DownloadWithPowerShellFallback(
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    unsigned long long minBytesPerSecond,
    DWORD lowSpeedWindowMs,
    DWORD noProgressAbortMs,
    const std::wstring& itemName,
    size_t linkIndex,
    size_t linkTotal)
{
    ClearTransferStalledFlag();

    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";
    DeleteFileW(partPath.c_str());

    DWORD timeoutSec = std::max<DWORD>(30, timeoutMs / 1000);
    timeoutSec = std::min<DWORD>(timeoutSec, 900);

    std::wstring psUrl = EscapePowerShellSingleQuoted(url);
    std::wstring psPart = EscapePowerShellSingleQuoted(partPath);
    std::wstring psTimeout = std::to_wstring(timeoutSec);

    std::wstring script =
        L"$ProgressPreference='SilentlyContinue'; "
        L"$ErrorActionPreference='Stop'; "
        L"try { Add-Type -AssemblyName System.Net.Http } catch {}; "
        L"try { [Net.ServicePointManager]::SecurityProtocol = "
        L"[Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls11 -bor [Net.SecurityProtocolType]::Tls } catch {}; "
        L"$url='" + psUrl + L"'; "
        L"$out='" + psPart + L"'; "
        L"$ua='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36'; "
        L"try { "
        L"$handler=New-Object System.Net.Http.HttpClientHandler; "
        L"$handler.AllowAutoRedirect=$true; "
        L"$client=New-Object System.Net.Http.HttpClient -ArgumentList $handler; "
        L"$client.Timeout=[TimeSpan]::FromSeconds(" + psTimeout + L"); "
        L"$client.DefaultRequestHeaders.UserAgent.ParseAdd($ua); "
        L"$client.DefaultRequestHeaders.Accept.ParseAdd('application/octet-stream'); "
        L"$client.DefaultRequestHeaders.Accept.ParseAdd('application/zip'); "
        L"$client.DefaultRequestHeaders.Accept.ParseAdd('*/*'); "
        L"$resp=$client.GetAsync($url,[System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult(); "
        L"$resp.EnsureSuccessStatusCode() | Out-Null; "
        L"$stream=$resp.Content.ReadAsStreamAsync().GetAwaiter().GetResult(); "
        L"$file=[IO.File]::Open($out,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None); "
        L"$buf=New-Object byte[] 1048576; "
        L"try { while (($read=$stream.Read($buf,0,$buf.Length)) -gt 0) { $file.Write($buf,0,$read) } } "
        L"finally { $file.Close(); $stream.Close(); $client.Dispose(); if ($resp) { $resp.Dispose() } } "
        L"exit 0 "
        L"} catch { "
        L"try { "
        L"$wc=New-Object Net.WebClient; "
        L"$wc.Headers.Add('User-Agent',$ua); "
        L"$wc.Headers.Add('Accept','application/octet-stream,application/zip,*/*'); "
        L"$wc.Headers.Add('Accept-Language','ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6'); "
        L"$wc.DownloadFile($url,$out); "
        L"exit 0 "
        L"} catch { exit 1 } "
        L"}";

    std::wstring powerShellExe = GetSystemToolPath(L"WindowsPowerShell\\v1.0\\powershell.exe");
    std::wstring encoded = Base64EncodePowerShellCommand(script);
    std::wstring command =
        QuoteProcessArgument(powerShellExe) +
        L" -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + encoded;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    const wchar_t* appName = FileExists(powerShellExe) ? powerShellExe.c_str() : nullptr;

    if (!CreateProcessW(appName, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        DebugLogLine(L"download", L"CreateProcess failed command=" + command + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    if (!WaitForDownloadProcessWithInstantFallback(
        pi, partPath, timeoutMs,
        noProgressAbortMs, lowSpeedWindowMs, minBytesPerSecond,
        itemName, L"PowerShell", linkIndex, linkTotal))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    DebugLogLine(L"download", L"process exit command=" + command + L" exit=" + std::to_wstring(exitCode) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

    return true;
}


static bool DownloadWithWinInetFallback(
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    unsigned long long minBytesPerSecond,
    DWORD lowSpeedWindowMs,
    DWORD noProgressAbortMs,
    const std::wstring& itemName,
    size_t linkIndex,
    size_t linkTotal)
{
    ClearTransferStalledFlag();

    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";
    DeleteFileW(partPath.c_str());
    DebugLogLine(L"download", L"WinINet start item=" + itemName + L" url=" + url + L" target=" + targetPath +
        L" timeout=" + std::to_wstring(timeoutMs));

    HINTERNET session = InternetOpenW(
        L"Mozilla/5.0 HolyCheckDownloader/WinINet",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr,
        nullptr,
        0
    );

    if (!session)
    {
        DebugLogLine(L"download", L"WinINet InternetOpen failed item=" + itemName + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    DWORD connectTimeout = timeoutMs >= 300000 ? 6000 : 5000;
    DWORD receiveTimeout = IsProblemDownloadName(itemName) ? 7000 : 9000;
    DWORD sendTimeout = 5000;

    InternetSetOptionW(session, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    InternetSetOptionW(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout));
    InternetSetOptionW(session, INTERNET_OPTION_SEND_TIMEOUT, &sendTimeout, sizeof(sendTimeout));

    std::wstring headers =
        L"Accept: application/octet-stream,application/zip,application/x-msdownload,*/*\r\n"
        L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6\r\n"
        L"Accept-Encoding: identity\r\n"
        L"Cache-Control: no-cache\r\n"
        L"Pragma: no-cache\r\n";

    DWORD flags = INTERNET_FLAG_RELOAD |
        INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_PRAGMA_NOCACHE |
        INTERNET_FLAG_NO_UI |
        INTERNET_FLAG_KEEP_CONNECTION;

    HINTERNET handle = InternetOpenUrlW(
        session,
        url.c_str(),
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        flags,
        0
    );

    if (!handle)
    {
        DebugLogLine(L"download", L"WinINet InternetOpenUrl failed item=" + itemName + L" error=" + FormatWin32ErrorForLog(GetLastError()) + L" url=" + url);
        InternetCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (HttpQueryInfoW(handle, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, nullptr))
    {
        if (status < 200 || status >= 300)
        {
            DebugLogLine(L"download", L"WinINet HTTP status failed item=" + itemName + L" status=" + std::to_wstring(status));
            InternetCloseHandle(handle);
            InternetCloseHandle(session);
            return false;
        }
    }

    HANDLE file = CreateFileW(
        partPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
    {
        DebugLogLine(L"download", L"WinINet part open failed item=" + itemName + L" part=" + partPath + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        InternetCloseHandle(handle);
        InternetCloseHandle(session);
        return false;
    }

    const DWORD BUFFER_SIZE = 1024 * 1024;
    std::vector<unsigned char> buffer(BUFFER_SIZE);
    unsigned long long totalBytes = 0;

    ULONGLONG started = GetTickCount64();
    ULONGLONG lastProgress = started;
    ULONGLONG speedWindowStarted = started;
    unsigned long long speedWindowBytes = 0;
    ULONGLONG lowSpeedWindowStarted = started;
    unsigned long long lowSpeedWindowBytes = 0;
    ULONGLONG lastUiUpdate = 0;

    UpdateActiveDownloadStatus(itemName, L"WinINet", linkIndex, linkTotal, 0, 0);

    bool ok = true;

    for (;;)
    {
        if (IsCancelRequested() || GetTickCount64() - started > timeoutMs)
        {
            ok = false;
            MarkTransferStalled();
            break;
        }

        DWORD read = 0;
        if (!InternetReadFile(handle, buffer.data(), BUFFER_SIZE, &read))
        {
            ok = false;
            MarkTransferStalled();
            break;
        }

        if (read == 0)
            break;

        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)
        {
            ok = false;
            break;
        }

        totalBytes += written;
        lastProgress = GetTickCount64();

        ULONGLONG now = lastProgress;

        if (now - speedWindowStarted >= 1000)
        {
            unsigned long long delta = totalBytes >= speedWindowBytes ? totalBytes - speedWindowBytes : 0;
            g_lastDownloadSpeedBps = (delta * 1000ull) / std::max<ULONGLONG>(1, now - speedWindowStarted);
            speedWindowStarted = now;
            speedWindowBytes = totalBytes;
        }

        if (now - lastUiUpdate >= 500)
        {
            UpdateActiveDownloadStatus(itemName, L"WinINet", linkIndex, linkTotal, totalBytes, g_lastDownloadSpeedBps);
            lastUiUpdate = now;
        }

        if (noProgressAbortMs > 0 && now - lastProgress >= noProgressAbortMs)
        {
            ok = false;
            MarkTransferStalled();
            break;
        }

        if (lowSpeedWindowMs > 0 && minBytesPerSecond > 0)
        {
            ULONGLONG windowMs = now - lowSpeedWindowStarted;
            if (windowMs >= lowSpeedWindowMs)
            {
                unsigned long long delta = totalBytes >= lowSpeedWindowBytes ? totalBytes - lowSpeedWindowBytes : 0;
                unsigned long long bytesPerSecond = (delta * 1000ull) / std::max<ULONGLONG>(1, windowMs);

                if (bytesPerSecond < minBytesPerSecond)
                {
                    ok = false;
                    MarkTransferStalled();
                    break;
                }

                lowSpeedWindowStarted = now;
                lowSpeedWindowBytes = totalBytes;
            }
        }
    }

    CloseHandle(file);
    InternetCloseHandle(handle);
    InternetCloseHandle(session);

    if (!ok || !FileExists(partPath) || IsCancelRequested())
    {
        DebugLogLine(L"download", L"WinINet failed item=" + itemName + L" ok=" + std::to_wstring(ok ? 1 : 0) +
            L" partExists=" + std::to_wstring(FileExists(partPath) ? 1 : 0) +
            L" cancelled=" + std::to_wstring(IsCancelRequested() ? 1 : 0) +
            L" bytes=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)));
        return TryFinalizeAbortedPartFile(itemName, partPath, targetPath);
    }

    DeleteFileW(targetPath.c_str());
    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
    {
        DebugLogLine(L"download", L"WinINet MoveFile failed item=" + itemName + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    DebugLogLine(L"download", L"WinINet success item=" + itemName + L" bytes=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
    return true;
}

static bool FindFirstExeRecursive(const std::wstring& folder, std::wstring& outPath)
{
    std::wstring mask = CombinePath(folder, L"*");

    WIN32_FIND_DATAW data{};
    HANDLE hFind = FindFirstFileW(mask.c_str(), &data);

    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    std::vector<std::wstring> subFolders;

    do
    {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        std::wstring path = CombinePath(folder, data.cFileName);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            subFolders.push_back(path);
        }
        else if (EndsWithI(path, L".exe"))
        {
            outPath = path;
            FindClose(hFind);
            return true;
        }
    }
    while (FindNextFileW(hFind, &data));

    FindClose(hFind);

    for (const auto& subFolder : subFolders)
    {
        if (FindFirstExeRecursive(subFolder, outPath))
            return true;
    }

    return false;
}


static std::wstring MakeSafeFileStem(std::wstring name)
{
    for (wchar_t& ch : name)
    {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'\"' || ch == L'<' || ch == L'>' || ch == L'|')
        {
            ch = L'_';
        }
    }

    while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
        name.pop_back();

    if (name.empty())
        name = L"program";

    return name;
}

static std::wstring GetItemFinalExePath(const DownloadItem& item, const std::wstring& baseFolder)
{
    std::wstring tabFolder = CombinePath(baseFolder, GetTabFolderName(item.tab));

    if (EndsWithI(item.file, L".zip"))
        return CombinePath(tabFolder, MakeSafeFileStem(item.name) + L".exe");

    return CombinePath(tabFolder, item.file);
}

static std::wstring GetItemTargetPath(const DownloadItem& item, const std::wstring& baseFolder)
{
    std::wstring tabFolder = CombinePath(baseFolder, GetTabFolderName(item.tab));
    return CombinePath(tabFolder, item.file);
}

static std::wstring GetItemExtractDir(const DownloadItem& item, const std::wstring& baseFolder)
{
    std::wstring tabFolder = CombinePath(baseFolder, GetTabFolderName(item.tab));
    return CombinePath(tabFolder, RemoveExtension(item.file));
}

static std::wstring TrimLaunchString(std::wstring value)
{
    while (!value.empty() && iswspace(value.front()))
        value.erase(value.begin());

    while (!value.empty() && iswspace(value.back()))
        value.pop_back();

    while (!value.empty() && (value.front() == L'\"' || value.front() == L'\''))
        value.erase(value.begin());

    while (!value.empty() && (value.back() == L'\"' || value.back() == L'\'' || value.back() == L','))
        value.pop_back();

    while (!value.empty() && iswspace(value.back()))
        value.pop_back();

    return value;
}

static std::wstring ExtractExecutablePathFromRegistryValue(std::wstring value)
{
    value = TrimLaunchString(value);

    if (value.empty())
        return L"";

    if (value.front() == L'\"')
    {
        size_t endQuote = value.find(L'\"', 1);
        if (endQuote != std::wstring::npos)
            return TrimLaunchString(value.substr(1, endQuote - 1));
    }

    std::wstring lower = ToLower(value);
    size_t exePos = lower.find(L".exe");

    if (exePos != std::wstring::npos)
        return TrimLaunchString(value.substr(0, exePos + 4));

    size_t commaPos = value.find(L',');
    if (commaPos != std::wstring::npos)
        value = value.substr(0, commaPos);

    return TrimLaunchString(value);
}

static bool LaunchRegistryGetString(HKEY key, const wchar_t* valueName, std::wstring& out)
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
        DWORD expandedLen = ExpandEnvironmentStringsW(value.c_str(), expanded, ARRAYSIZE(expanded));

        if (expandedLen > 0 && expandedLen < ARRAYSIZE(expanded))
            value = expanded;
    }

    out = value;
    return !out.empty();
}

static void AddUniqueSystemInformerLaunchCandidate(std::vector<std::wstring>& candidates, const std::wstring& rawPath)
{
    std::wstring path = ExtractExecutablePathFromRegistryValue(rawPath);

    if (path.empty())
        return;

    std::wstring lower = ToLower(path);
    bool systemInformerName = lower.find(L"systeminformer") != std::wstring::npos ||
        (lower.find(L"system") != std::wstring::npos && lower.find(L"informer") != std::wstring::npos);

    if (!EndsWithI(lower, L".exe") || !systemInformerName)
        return;

    for (const std::wstring& existing : candidates)
    {
        if (ToLower(existing) == lower)
            return;
    }

    candidates.push_back(path);
}

static void AddSystemInformerCandidateFromEnv(std::vector<std::wstring>& candidates, const wchar_t* envName, const wchar_t* relativePath)
{
    wchar_t base[MAX_PATH * 4]{};
    DWORD len = GetEnvironmentVariableW(envName, base, ARRAYSIZE(base));

    if (len == 0 || len >= ARRAYSIZE(base))
        return;

    AddUniqueSystemInformerLaunchCandidate(candidates, CombinePath(base, relativePath));
}

static void AddSystemInformerAppPathRegistryCandidates(std::vector<std::wstring>& candidates, HKEY root, REGSAM wowFlags)
{
    HKEY key{};
    if (RegOpenKeyExW(root,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\SystemInformer.exe",
            0, KEY_READ | wowFlags, &key) != ERROR_SUCCESS)
    {
        return;
    }

    std::wstring value;
    if (LaunchRegistryGetString(key, nullptr, value))
        AddUniqueSystemInformerLaunchCandidate(candidates, value);

    RegCloseKey(key);
}

static void AddSystemInformerUninstallRegistryCandidates(std::vector<std::wstring>& candidates, HKEY root, REGSAM wowFlags)
{
    HKEY uninstallKey{};
    if (RegOpenKeyExW(root,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
            0, KEY_READ | wowFlags, &uninstallKey) != ERROR_SUCCESS)
    {
        return;
    }

    for (DWORD index = 0;; ++index)
    {
        wchar_t subName[512]{};
        DWORD subNameLen = ARRAYSIZE(subName);

        if (RegEnumKeyExW(uninstallKey, index, subName, &subNameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        HKEY itemKey{};
        if (RegOpenKeyExW(uninstallKey, subName, 0, KEY_READ | wowFlags, &itemKey) != ERROR_SUCCESS)
            continue;

        std::wstring displayName;
        if (LaunchRegistryGetString(itemKey, L"DisplayName", displayName))
        {
            std::wstring lowerDisplayName = ToLower(displayName);
            if (lowerDisplayName.find(L"systeminformer") != std::wstring::npos ||
                (lowerDisplayName.find(L"system") != std::wstring::npos && lowerDisplayName.find(L"informer") != std::wstring::npos))
            {
                std::wstring installLocation;
                if (LaunchRegistryGetString(itemKey, L"InstallLocation", installLocation))
                {
                    installLocation = TrimLaunchString(installLocation);
                    if (!installLocation.empty())
                    {
                        AddUniqueSystemInformerLaunchCandidate(candidates, CombinePath(installLocation, L"SystemInformer.exe"));
                        AddUniqueSystemInformerLaunchCandidate(candidates, CombinePath(installLocation, L"System Informer.exe"));
                    }
                }

                std::wstring displayIcon;
                if (LaunchRegistryGetString(itemKey, L"DisplayIcon", displayIcon))
                    AddUniqueSystemInformerLaunchCandidate(candidates, displayIcon);
            }
        }

        RegCloseKey(itemKey);
    }

    RegCloseKey(uninstallKey);
}

static std::wstring FindInstalledSystemInformerPath()
{
    std::vector<std::wstring> candidates;

    AddSystemInformerAppPathRegistryCandidates(candidates, HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    AddSystemInformerAppPathRegistryCandidates(candidates, HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
    AddSystemInformerAppPathRegistryCandidates(candidates, HKEY_CURRENT_USER, 0);

    AddSystemInformerUninstallRegistryCandidates(candidates, HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    AddSystemInformerUninstallRegistryCandidates(candidates, HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
    AddSystemInformerUninstallRegistryCandidates(candidates, HKEY_CURRENT_USER, 0);

    AddSystemInformerCandidateFromEnv(candidates, L"ProgramFiles", L"SystemInformer\\SystemInformer.exe");
    AddSystemInformerCandidateFromEnv(candidates, L"ProgramFiles", L"System Informer\\SystemInformer.exe");
    AddSystemInformerCandidateFromEnv(candidates, L"ProgramFiles(x86)", L"SystemInformer\\SystemInformer.exe");
    AddSystemInformerCandidateFromEnv(candidates, L"ProgramFiles(x86)", L"System Informer\\SystemInformer.exe");
    AddSystemInformerCandidateFromEnv(candidates, L"LOCALAPPDATA", L"Programs\\SystemInformer\\SystemInformer.exe");
    AddSystemInformerCandidateFromEnv(candidates, L"LOCALAPPDATA", L"Programs\\System Informer\\SystemInformer.exe");

    for (const std::wstring& path : candidates)
    {
        if (FileExists(path))
            return path;
    }

    return L"";
}

static std::wstring FindLaunchPathForItem(const DownloadItem& item, const std::wstring& baseFolder)
{
    std::wstring target = GetItemTargetPath(item, baseFolder);

    if (IsSystemInformerItem(item))
    {
        std::wstring installedPath = FindInstalledSystemInformerPath();
        if (!installedPath.empty())
            return installedPath;
    }

    if (EndsWithI(target, L".zip"))
    {
        std::wstring extractDir = GetItemExtractDir(item, baseFolder);
        std::wstring exePath;

        if (FindFirstExeRecursive(extractDir, exePath))
            return exePath;

        return L"";
    }

    if (FileExists(target))
        return target;

    return L"";
}

static bool OpenPath(const std::wstring& path)
{
    if (path.empty())
        return false;

    HINSTANCE result = ShellExecuteW(
        g_hMain,
        L"open",
        path.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(result) > 32;
}

static bool OpenPathElevated(const std::wstring& path)
{
    if (path.empty())
        return false;

    HINSTANCE result = ShellExecuteW(
        g_hMain,
        L"runas",
        path.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(result) > 32;
}


static const wchar_t* GetEverythingCheatsQuery();

static bool IsEverythingItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"everything") != std::wstring::npos;
}

static std::wstring QuotePowerShellSingleArgLocal(std::wstring value)
{
    std::wstring out;
    for (wchar_t ch : value)
        out += (ch == L'\'') ? L"''" : std::wstring(1, ch);
    return out;
}

static bool EnsureRegistryExplorerDotNetRuntime(const std::wstring& registryExplorerExe)
{
    if (registryExplorerExe.empty())
        return false;

    std::wstring exeDir;
    size_t slash = registryExplorerExe.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        exeDir = registryExplorerExe.substr(0, slash);

    std::wstring script;
    script.reserve(8192);
    script += L"$ErrorActionPreference='Stop'\n";
    script += L"$exe='" + QuotePowerShellSingleArgLocal(registryExplorerExe) + L"'\n";
    script += L"$dir='" + QuotePowerShellSingleArgLocal(exeDir) + L"'\n";
    script += LR"HCPS(
function Out($s){[Console]::WriteLine([string]$s)}
$runtimeName='Microsoft.WindowsDesktop.App'
$major='8'
try{
  $runtimeConfig=Get-ChildItem -LiteralPath $dir -Filter '*.runtimeconfig.json' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*RegistryExplorer*' } | Select-Object -First 1
  if($runtimeConfig){
    $json=Get-Content -LiteralPath $runtimeConfig.FullName -Raw | ConvertFrom-Json
    $fx=@($json.runtimeOptions.frameworks)+@($json.runtimeOptions.framework) | Where-Object { $_ }
    $desktop=$fx | Where-Object { $_.name -eq 'Microsoft.WindowsDesktop.App' } | Select-Object -First 1
    if($desktop -and $desktop.version){ $major=([string]$desktop.version).Split('.')[0] }
  }
}catch{}
try{
  $runtimes=& dotnet --list-runtimes 2>$null
  if($LASTEXITCODE -eq 0 -and ($runtimes -match ('^' + [regex]::Escape($runtimeName) + '\s+' + [regex]::Escape($major) + '\.'))){ Out 'runtime-ok'; exit 0 }
}catch{}
$temp=Join-Path ([IO.Path]::GetTempPath()) 'HolyCheck\RegistryExplorerRuntime'
New-Item -ItemType Directory -Force -Path $temp | Out-Null
$installer=Join-Path $temp ('windowsdesktop-runtime-' + $major + '.0-win-x64.exe')
$url='https://aka.ms/dotnet/' + $major + '.0/windowsdesktop-runtime-win-x64.exe'
Out ('runtime-missing: ' + $runtimeName + ' ' + $major + '.x')
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing -TimeoutSec 120
$sig=Get-AuthenticodeSignature -LiteralPath $installer
if($sig.Status -ne 'Valid'){ throw 'Installer signature is not valid: ' + $sig.Status }
$p=Start-Process -FilePath $installer -ArgumentList '/install','/quiet','/norestart' -Wait -PassThru
if($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010){ throw 'Installer exit code: ' + $p.ExitCode }
Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue
Out 'runtime-installed'
)HCPS";

    std::wstring output;
    bool ok = RunHiddenPowerShellScriptCapture(script, output, 180000);
    if (!ok)
    {
        PostLog(L"RegistryExplorer: не удалось проверить .NET Desktop Runtime");
        return false;
    }

    std::wstring lower = ToLower(output);
    if (lower.find(L"runtime-ok") != std::wstring::npos || lower.find(L"runtime-installed") != std::wstring::npos)
        return true;

    PostLog(L"RegistryExplorer: не удалось установить Microsoft.WindowsDesktop.App Runtime");
    if (!output.empty())
        DebugLogLine(L"registryexplorer", L"runtime output=" + output);
    return false;
}

static bool OpenEverythingPath(const std::wstring& path)
{
    if (path.empty())
        return false;

    std::wstring params = L"-search ";
    params += QuoteProcessArgument(GetEverythingCheatsQuery());

    HINSTANCE result = ShellExecuteW(
        g_hMain,
        L"open",
        path.c_str(),
        params.c_str(),
        nullptr,
        SW_SHOWNORMAL
    );

    bool ok = reinterpret_cast<INT_PTR>(result) > 32;
    if (ok)
        PostLog(L"Everything открыт с заполненным поиском");
    return ok;
}

static bool ShouldOpenItemElevated(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);

    return name.find(L"registryexplorer") != std::wstring::npos ||
        (name.find(L"registry") != std::wstring::npos && name.find(L"explorer") != std::wstring::npos) ||
        name.find(L"recuva") != std::wstring::npos;
}

static bool OpenItemPath(const DownloadItem& item, const std::wstring& path)
{
    if (IsEverythingItem(item))
        return OpenEverythingPath(path);

    if (IsRegistryExplorerItem(item) && !EnsureRegistryExplorerDotNetRuntime(path))
        return false;

    if (ShouldOpenItemElevated(item))
        return OpenPathElevated(path);

    return OpenPath(path);
}

static bool CopyTextToClipboard(const wchar_t* text)
{
    if (!text || !g_hMain)
        return false;

    size_t len = wcslen(text);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));

    if (!memory)
        return false;

    wchar_t* target = static_cast<wchar_t*>(GlobalLock(memory));

    if (!target)
    {
        GlobalFree(memory);
        return false;
    }

    memcpy(target, text, (len + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);

    if (!OpenClipboard(g_hMain))
    {
        GlobalFree(memory);
        return false;
    }

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return true;
}

static void CopyHelperText(const wchar_t* text, const std::wstring& name)
{
    UNREFERENCED_PARAMETER(name);

    if (CopyTextToClipboard(text))
        PostLog(L"текст скопирован");
    else
        PostLog(L"текст не скопирован");
}

static const wchar_t* EVERYTHING_QUERY_1 = LR"HCEV1(ext:.exe;.jar;.zip;.rar regex:(impact|wurst|bleach[-*]?hack|aristois|huzuni|skill[-*]?client|nodus|inertia|ares|sigma|meteor|BetterHitReg|atomic|zamorozka|liquid[-*]?bounce|nurik|nursultan|celestial|calestial|celka|expensive|neverhook|excellent|wexside|wild|minced|deadcode|akrien|future|dreampool|vape|infinity|squad|no[-*]?rules|konas|zeus[-*]?client|rich[-*]?client|ghost[-*]?client|rusher[-*]?hack|thunder[-*]?hack|moon[-*]?hack|winner|nova|exire|doomsday|nightware|ricardo|extazyy|troxill|arbuz|dauntiblyat|rename[-*]?me[-*]?please|edit[-*]?me|takker|faker|xameleon|fuze[-*]?client|wise[-*]?folder|net[-*]?limiter|feather|delta|eclipse|venus|jex|hakari|hush|hach|rogalik|catlavan|haruka|wissend|fluger|sperma|vortex|newcode|astra|britva|bariton|bot|player|freecam|bedrock|hotbar|swap|chest|gumball|tweak|entity|crystal|lowdurabilityswitcher|optimizer|viabackwards|viaforge|viaproxy|hitbox|elytra|through|mob|auto|place|health|inventory|x[-*]?ray|clean[-*]?cut|smart[-*]?moving|save[-*]?searcher|world[-*]?downloader|trade[-*]?finder|chorus[-*]?find|inv[-*]?move|chunk[-*]?copy|seed[-*]?cracker|diamond[-*]?sim|forge[-*]?hax|step[-*]?up|client[-*]?commands|camera[-*]?utils|cheat[-*]?utils|universal[-*]?mod|swing[-*]?through[-*]?grass|Destra[-*]?Visuals|minimap))HCEV1";

static const wchar_t* GetEverythingCheatsQuery()
{
    return EVERYTHING_QUERY_1;
}


static const wchar_t* EVERYTHING_QUERY_2 = LR"HCEV2(ext:.txt;.json;.toml;.yml;.cfg;.properties;.ini | folder: dm:last14days regex:(shellbag|bariton|bot|player|freecam|bedrock|hotbar|swap|chest|gumball|tweak|entity|crystal|optimizer|viabackwards|viaforge|viaproxy|hitbox|elytra|through|mob|auto|place|health|inventory|x[-_]?ray|clean[-_]?cut|smart[-_]?moving|save[-_]?searcher|world[-_]?downloader|trade[-_]?finder|chorus[-_]?find|inv[-_]?move|chunk[-_]?copy|seed[-_]?cracker|diamond[-_]?sim|forge[-_]?hax|step[-_]?up|client[-_]?commands|camera[-_]?utils|cheat[-_]?utils|universal[-_]?mod|swing[-_]?through[-_]?grass))HCEV2";



static const wchar_t* JOURNAL_TRACE_TXT = LR"HCJOURNAL(1
directory:.mineceaft;name:!!.
2
directory:.minecraft;directory:config||mods||logs||addons
3
directory::;name:.jar||.zip||.rar;name:!!.lnk
4
directory:Downloads||Desktop
5
directory:prefetch)HCJOURNAL";


static const wchar_t* SYSTEM_INFORMER_TXT = LR"HCSYSTEM(Regex (case-insensetive) | javaw.exe:

(ASM:|H\(q=Zf@wDLF\?/d\?D&|x/fC5K<;i/f6I\-w\[|SMe;8#xj0s<\+pyE&bEZ|cR_A\^yvDVSXDYPQp\]|cortex|r\|\~\}Xnpo\>\]_\^'@BA|WYX\-gih\>rssK\{\|\|\[|net\/minecraft\/client\/renderer\/RenderState\$\$Lambda\+0x000001e7d29d9930|7ba\{vsdkvhd\}o\}w|Hba\{vsdkvhduy\{q|Fba\{vsdkvhdpxhz|Aba\{vsdkvhdnxhv|x\/mo\/c|Self Destruct|blessed|stubborn\.website|cortex|E S P|Hitbox:|Reach:|chs\/Main|chs\/Profiler|net\.minecraftforge\.ASMEventHandler\.31\.wait\(long, int\)|magicthein|radioegor146|BaoBab:|b\/time|chs\/90|by\.kayn|ZDCoder|dreampool|TriggerBOT|forge\.commons\.|sjuuOiqotmus|<!+CJR|GUI Blur)


Regex (case-insensetive) | DPS:

(?:2023\/03\/09:17:11:30|2023\/07\/03:22:01:11|2023\/10\/04:16:07:05|2024\/04\/05:11:43:39|2025\/05\/28:23:57:12|2076\/05\/18:04:53:15|2024\/05\/02:22:48|2024\/09\/06:09:35:50|2023\/08\/13:13:09:58|2025\/01\/20:21:37:53|2025\/01\/16:14:21:21|2022\/06\/07:07:45:55)

> Список dps для справки (НЕ ДЛЯ БАНА!)

2023/03/09:17:11:30 - stubborn
2023/07/03:22:01:11 - синий авалон
2023/10/04:16:07:05 - зелёный авалон
2024/04/05:11:43:39 - красный авалон
2025/05/28:23:57:12 - cortex
2076/05/18:04:53:15 - unicorn
2024/05/02:22:48 - niobium
2024/09/06:09:35:50 - ocean bypass
2023/08/13:13:09:58 - rockstar
2025/01/20:21:37:53 - troxill
2025/01/16:14:21:21 - nemezida
2022/06/07:07:45:55 - mp3)HCSYSTEM";

static const wchar_t* POWERSHELL_TXT =
    LR"HCPS($ErrorActionPreference="SilentlyContinue";$out="$PWD\check.txt";$twinksOut="$PWD\checktwinks.txt";$mac = (Get-NetAdapter -Physical | Where-Object {$_.Status -eq 'Up'} | Sort-Object -Property InterfaceIndex | Select-Object -First 1).MacAddress;if (-not $mac) { $mac = (Get-NetAdapter | Where-Object {$_.Status -eq 'Up' -and $_.InterfaceDescription -notmatch 'Virtual|WireGuard|TAP'} | Sort-Object -Property InterfaceIndex | Select-Object -First 1).MacAddress };"1. Модель системы: $((Get-WmiObject Win32_ComputerSystem).Model)`n2. Серийный BIOS: $((Get-WmiObject Win32_BIOS).SerialNumber)`n3. Реестр: $((Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS').SystemProductName)`n4. Модель диска: $((Get-WmiObject Win32_DiskDrive | ForEach-Object { "$($_.Model) ($([math]::Round($_.Size/1GB, 2)) GB)" }) -join ', ')`n5. Видеоадаптеры: $(($(Get-CimInstance Win32_PnPSignedDriver | Where-Object {$_.DeviceClass -eq 'DISPLAY'} | ForEach-Object { "$($_.DeviceName) | $($_.Manufacturer) | $($_.DriverProviderName)" }) -join '; '))`n6. Виртуальные устройства: $((Get-WmiObject Win32_PnPEntity | Where-Object {$_.Name -match 'VMware|Virtual|VBox|Hyper-V|QEMU|Xen|KVM|VirtIO|VirtualBox|VMCI|SVGA|VMBus'}).Name -join ', ')`n7. Mac: $mac">$out;$bootTime=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;function f($t){$p=(' '*((45-$t.Length)/2));"${p}${t}${p}"+(' '*(45-$p.Length*2-$t.Length))};"`n`n=============================================`n$(f 'ИНФОРМАЦИЯ О ВКЛЮЧЕНИИ ПК')`n=============================================`nПоследнее включение ПК: $($bootTime.ToString('yyyy-MM-dd HH:mm:ss'))">>$out;"`n`n`n=============================================`n$(f 'СОСТОЯНИЕ СЛУЖБ')`n=============================================">>$out;$services=@("PcaSvc","DPS","SysMain","EventLog","bam")|%{$s=Get-Service $_ -ea 0;if($s){$t=(Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$_" -n Start -ea 0).Start;$n=switch($t){2{'Авто'}3{'Вручную'}4{'Отключена'}default{'Неизвестно'}};$p=if($s.Status-eq'Running'){try{$procId=(Get-WmiObject Win32_Service -Filter "Name='$_'").ProcessID;(Get-Process -Id $procId -ea 0).StartTime.ToString('yyyy-MM-dd HH:mm:ss')}catch{'Не удалось получить'}}else{'Не запущена'};[PSCustomObject]@{'Служба'=$_.PadRight(8);'Состояние'=$s.Status.ToString().PadRight(10);'Тип запуска'=$n.PadRight(9);'Время запуска'=$p.PadRight(20)}}};($services|ft -AutoSize -Wrap|Out-String -Width 4096|%{$_.Trim()})>>$out;"`n`n`n=============================================`n$(f 'СОБЫТИЯ ОЧИСТКИ ЖУРНАЛОВ (ID 104)')`n=============================================">>$out;$e=Get-WinEvent -LogName System -FilterXPath "*[System[(EventID=104)]]" -ea 0;if($e){$e|%{$d=($_.Message-split"`n")[0];"ID $($_.Id) | $($_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')) | $d">>$out}};"`n`n`n=============================================`n$(f 'СОБЫТИЯ ПРИЛОЖЕНИЯ (ID 3079)')`n=============================================">>$out;Get-WinEvent -LogName Application -FilterXPath "*[System[(EventID=3079)]]" -MaxEvents 5 -ea 0|%{$m=($_.Message-split"Подробности:")[-1].Trim()-replace"`n|`r|\s+"," ";"ID $($_.Id) | $($_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')) | $m">>$out};"`n`n`n=============================================`n$(f 'СПИСОК ТВИНКОВ ИГРОКА')`n=============================================">>$out;$r=@{};$err=@();)HCPS"
    LR"HCPS(Get-ChildItem -Path . -Recurse -Include *.log,*.log.gz -ea 0|%{try{if($_.Extension-eq'.gz'){$c=[System.IO.Compression.GzipStream]::new([System.IO.File]::OpenRead($_.FullName),[System.IO.Compression.CompressionMode]::Decompress);$s=[System.IO.StreamReader]::new($c);while(-not$s.EndOfStream){$l=$s.ReadLine();if($l-match'setting user:\s*(.+)$'){$r[$matches[1].Trim()]=1}};$s.Close()}else{Get-Content $_.FullName|?{$_-match'setting user:\s*(.+)$'}|%{$r[$matches[1].Trim()]=1}}}catch{$err+="Ошибка логов $($_.Name): $_"}};if(Test-Path "$env:APPDATA\.iasx"){try{[System.Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes("$env:APPDATA\.iasx")) -split '[\0-\x1F\x7F-\x9F]'|%{$_.TrimEnd('t')}|?{$_-match '^[\p{IsCyrillic}\p{IsBasicLatin}_]{3,20}$'-and$_-notmatch'=$'-and$_-notmatch'(java|alias|user|pass|field|object|count|list|data|xp|sr|lastused|premium|obj|q$|\[)'}|%{$r[$_]=1}}catch{$err+="Ошибка appdata iasx: $_"}};Get-ChildItem -Path . -Recurse -Filter *.iasx -ea 0|%{try{[System.Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($_.FullName)) -split '[\0-\x1F\x7F-\x9F]'|%{$_.TrimEnd('t')}|?{$_-match '^[\p{IsCyrillic}\p{IsBasicLatin}_]{3,20}$'-and$_-notmatch'=$'-and$_-notmatch'(java|alias|user|pass|field|object|count|list|data|xp|sr|lastused|premium|obj|q$|\[)'}|%{$r[$_]=1}}catch{$err+="Ошибка iasx $($_.Name): $_"}};Get-ChildItem -Path . -Recurse -Filter accounts_v1.do_not_send_to_anyone -ea 0|%{try{$b=[System.IO.File]::ReadAllBytes($_.FullName);$ms=New-Object System.IO.MemoryStream(,$b[2..($b.Length-1)]);$ds=New-Object System.IO.Compression.DeflateStream($ms,[System.IO.Compression.CompressionMode]::Decompress);$sr=New-Object System.IO.StreamReader($ds);$c=$sr.ReadToEnd();$sr.Close();$ds.Close();$ms.Close();$c-split'[\x00-\x1F]'|?{$_-match'^[a-zA-Z0-9_]{3,}$'}|%{$r[$_]=1}}catch{$err+="Ошибка accounts_v1 $($_.Name): $_"}};if(Test-Path "$PWD\usercache.json"){try{Get-Content "$PWD\usercache.json" -Raw|ConvertFrom-Json|Select-Object -ExpandProperty name|%{$r[$_]=1}}catch{$err+="Ошибка usercache.json: $_"}};if(Test-Path "$PWD\TlauncherProfiles.json"){try{Get-Content "$PWD\TlauncherProfiles.json" -Raw|ConvertFrom-Json|%{$_.accounts.PSObject.Properties.Value|Select-Object -Expand username}|%{$r[$_]=1}}catch{$err+="Ошибка TlauncherProfiles.json: $_"}};if(Test-Path "$PWD\LabyMod\accounts.json"){try{Get-Content "$PWD\LabyMod\accounts.json" -Raw|ConvertFrom-Json|%{$_.accounts.PSObject.Properties.Value.username}|%{$r[$_]=1}}catch{$err+="Ошибка LabyMod/accounts.json: $_"}};if(Test-Path "$PWD\labymod-neo\accounts.json"){try{Get-Content "$PWD\labymod-neo\accounts.json" -Raw|ConvertFrom-Json|%{$_.accounts.PSObject.Properties.Value.username}|%{$r[$_]=1}}catch{$err+="Ошибка labymod-neo/accounts.json: $_"}};Get-ChildItem -Path . -Recurse -Filter ias.json -ea 0|%{try{$j=Get-Content $_.FullName -Raw|ConvertFrom-Json;if($j.accounts){$j.accounts|%{if($_.name){$r[$_.name]=1}elseif($_.data.username){$r[$_.data.username]=1}}}}catch{$err+="Ошибка ias.json $($_.Name): $_"}};if(Test-Path "$PWD\usernamecache.json"){try{Get-Content "$PWD\usernamecache.json" -Raw|ConvertFrom-Json|%{$_.PSObject.Properties.Value}|%{$r[$_]=1}}catch{$err+="Ошибка usernamecache.json: $_"}};if(Test-Path "$env:APPDATA\.lunarclient"){try{Get-ChildItem "$env:APPDATA\.lunarclient" -Recurse -Filter *.json -ea 0|%{try{$c=Get-Content $_.FullName -Raw;)HCPS"
    LR"HCPS(if($c -match'"username"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}if($c -match'"displayName"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}}catch{$err+="Ошибка Lunar Client $($_.Name): $_"}}}catch{$err+="Ошибка доступа к Lunar Client: $_"}};if(Test-Path "$env:APPDATA\lunarclient"){try{Get-ChildItem "$env:APPDATA\lunarclient" -Recurse -Filter *.json -ea 0|%{try{$c=Get-Content $_.FullName -Raw;if($c -match'"username"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}if($c -match'"displayName"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}}catch{$err+="Ошибка Lunar Client $($_.Name): $_"}}}catch{$err+="Ошибка доступа к Lunar Client: $_"}};if($r.Count-gt0){($r.Keys-join' ')>$twinksOut;$r.Keys>>$out}else{"Нет данных о твинках">>$out};if($err){"Ошибки:`n$($err-join"`n")">>$out};$pwdPath=(Get-Location).ProviderPath;try{[System.IO.File]::WriteAllText((Join-Path $env:TEMP 'HolyChecker_last_check_dir.txt'),$pwdPath,[System.Text.Encoding]::UTF8)}catch{};try{Start-Process explorer.exe -ArgumentList $pwdPath}catch{})HCPS"
    ;

static const wchar_t* POWERSHELL_SITES_TXT = LR"HCSITES($sites="anticheat.ac","github.com","voidtools.com","privazer.com","nirsoft.net","download.ericzimmermanstools.com","win-rar.com","simpleunlocker.ds1nc.ru"; function Test-Site($u){ $r=@(); if(Select-String -Path "$env:SystemRoot\System32\drivers\etc\hosts" -Pattern "(?mi)^\s*?\d.+?$($u.Replace('.','\.'))\s*$"){$r+="HOSTS"}; try{if((Resolve-DnsName $u -ErrorAction Stop -Type A|% IPAddress)-like'127.*'){$r+="DNS_LOOPBACK"}}catch{}; $regPath="HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings\ZoneMap\Domains\$($u.Replace('.','\\'))";if(Test-Path $regPath){$r+="REGISTRY_ZONE"}elseif(Test-Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings\ZoneMap\Domains\$u"){$r+="REGISTRY_ZONE"}; if(Get-NetFirewallRule|?{$_.DisplayName-like"*$u*"-and$_.Action-eq"Block"}){$r+="FIREWALL"}; try{iwr "https://$u"-TimeoutSec 5 -UseBasicParsing -DisableKeepAlive -ErrorAction Stop|Out-Null}catch{if($_.Exception.Response.StatusCode-eq407){$r+="PROXY_BLOCK"}elseif(-not$_.Exception.Response){try{iwr "http://$u"-TimeoutSec 3 -UseBasicParsing -DisableKeepAlive -ErrorAction Stop|Out-Null}catch{$r+="WEB_BLOCKED"}}}; $tcp80=Test-NetConnection $u -Port 80 -WarningAction SilentlyContinue; $tcp443=Test-NetConnection $u -Port 443 -WarningAction SilentlyContinue; if(-not$tcp80.TcpTestSucceeded-and$u-ne'anticheat.ac'){$r+="PORT_80_BLOCKED"};if(-not$tcp443.TcpTestSucceeded-and$u-ne'anticheat.ac'){$r+="PORT_443_BLOCKED"}; if($r.Count-gt0){"$u → БЛОКИРОВКА: $($r-join', ')"}else{"$u → OK"}}; $sites|%{Test-Site $_}
)HCSITES";


static bool WriteUtf8TextFile(const std::wstring& path, const wchar_t* text)
{
    if (!text)
        return false;

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (size <= 1)
        return false;

    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &utf8[0], size, nullptr, nullptr);

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    bool ok = WriteFile(file, bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);

    if (ok)
    {
        written = 0;
        ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size() - 1), &written, nullptr) &&
            written == static_cast<DWORD>(utf8.size() - 1);
    }

    CloseHandle(file);
    return ok;
}

static bool ItemNameEquals(const DownloadItem& item, const std::wstring& name)
{
    return ToLower(item.name) == ToLower(name);
}

static bool ReadFilePrefix(const std::wstring& path, std::vector<unsigned char>& out, DWORD maxBytes = 16)
{
    out.clear();

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    out.resize(maxBytes);
    DWORD read = 0;
    bool ok = ReadFile(file, out.data(), maxBytes, &read, nullptr) != FALSE;
    CloseHandle(file);

    if (!ok || read == 0)
    {
        out.clear();
        return false;
    }

    out.resize(read);
    return true;
}

static bool StartsWithBytes(const std::vector<unsigned char>& data, std::initializer_list<unsigned char> bytes)
{
    if (data.size() < bytes.size())
        return false;

    size_t i = 0;
    for (unsigned char b : bytes)
    {
        if (data[i++] != b)
            return false;
    }

    return true;
}

static bool LooksLikeDownloadedHtml(const std::vector<unsigned char>& data)
{
    if (data.empty())
        return false;

    std::string head;
    for (unsigned char c : data)
    {
        if (c >= 32 && c <= 126)
            head.push_back(static_cast<char>(tolower(c)));
    }

    return head.find("html") != std::string::npos ||
        head.find("doctype") != std::string::npos ||
        head.find("<head") != std::string::npos ||
        head.find("<body") != std::string::npos;
}


static bool IsZipArchiveStructurallyValid(const std::wstring& path)
{
    unsigned long long totalSize = 0;
    if (!GetFileSizeBytes(path, totalSize) || totalSize < 22)
        return false;

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD tailSize = static_cast<DWORD>(std::min<unsigned long long>(totalSize, 65557));
    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(totalSize - tailSize);

    if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN))
    {
        CloseHandle(file);
        return false;
    }

    std::vector<unsigned char> tail(tailSize);
    DWORD read = 0;
    bool ok = ReadFile(file, tail.data(), tailSize, &read, nullptr) != FALSE;
    CloseHandle(file);

    if (!ok || read < 22)
        return false;

    tail.resize(read);

    for (size_t i = tail.size() - 22; i != static_cast<size_t>(-1); --i)
    {
        if (i + 4 <= tail.size() &&
            tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06)
        {
            return true;
        }

        if (i == 0)
            break;
    }

    for (size_t i = 0; i + 4 <= tail.size(); ++i)
    {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B &&
            ((tail[i + 2] == 0x06 && tail[i + 3] == 0x06) ||
             (tail[i + 2] == 0x06 && tail[i + 3] == 0x07)))
        {
            return true;
        }
    }

    return false;
}

static bool IsValidDownloadedPayload(const DownloadItem& item, const std::wstring& path)
{
    if (!FileExists(path))
        return false;

    LARGE_INTEGER size{};
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    bool gotSize = GetFileSizeEx(file, &size) != FALSE;
    CloseHandle(file);

    if (!gotSize || size.QuadPart <= 0)
        return false;

    std::wstring fileLowerForSize = ToLower(item.file);

    if (EndsWithI(fileLowerForSize, L".exe"))
    {
        unsigned long long minExeSize = GetMinCompleteExeSizeForName(item.name);
        if (static_cast<unsigned long long>(size.QuadPart) < minExeSize)
            return false;
    }

    if ((EndsWithI(fileLowerForSize, L".zip") || EndsWithI(fileLowerForSize, L".jar")) && size.QuadPart < 22)
        return false;


    if (EndsWithI(fileLowerForSize, L".rar") && size.QuadPart < 7)
        return false;

    if (EndsWithI(fileLowerForSize, L".7z") && size.QuadPart < 6)
        return false;

    if (EndsWithI(fileLowerForSize, L".msi") && size.QuadPart < 512)
        return false;

    std::vector<unsigned char> head;
    if (!ReadFilePrefix(path, head, 32))
        return false;

    if (LooksLikeDownloadedHtml(head))
        return false;

    std::wstring fileLower = fileLowerForSize;

    if (EndsWithI(fileLower, L".exe"))
        return StartsWithBytes(head, { 0x4D, 0x5A });

    if (EndsWithI(fileLower, L".zip") || EndsWithI(fileLower, L".jar"))
        return StartsWithBytes(head, { 0x50, 0x4B }) && IsZipArchiveStructurallyValid(path);

    if (EndsWithI(fileLower, L".rar"))
        return StartsWithBytes(head, { 0x52, 0x61, 0x72, 0x21 });

    if (EndsWithI(fileLower, L".7z"))
        return StartsWithBytes(head, { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C });

    if (EndsWithI(fileLower, L".msi"))
        return StartsWithBytes(head, { 0xD0, 0xCF, 0x11, 0xE0 });

    return true;
}

static bool ValidateDownloadedFileOrDelete(const DownloadItem& item, const std::wstring& targetPath)
{
    bool valid = IsValidDownloadedPayload(item, targetPath);
    DebugLogLine(L"download", L"validate payload item=" + item.name + L" path=" + targetPath +
        L" valid=" + std::to_wstring(valid ? 1 : 0) +
        L" size=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
    if (valid)
        return true;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    MarkDownloadedPayloadInvalid();
    return false;
}

static bool DownloadRecursiveValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath);
static bool DownloadCurlValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath);
static bool DownloadPowerShellValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath)
{
    if (!DownloadWithPowerShellFallback(
            url, targetPath, GetDirectDownloadTimeoutMs(item),
            0, 0, GetPowerShellNoProgressAbortMs(item), item.name, 1, 1))
        return false;

    return ValidateDownloadedFileOrDelete(item, targetPath);
}

static bool IsPrimaryUrlOnlyItem(const DownloadItem& item)
{
    return IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item) || IsSimpleUnlockerItem(item);
}

static bool IsJournalTraceItem(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);
    return name.find(L"journaltrace") != std::wstring::npos ||
        (name.find(L"journal") != std::wstring::npos && name.find(L"trace") != std::wstring::npos);
}

static bool IsRecuvaItem(const DownloadItem& item)
{
    return ToLower(item.name).find(L"recuva") != std::wstring::npos;
}

static bool ShouldTryPowerShellBeforeCurl(const DownloadItem& item)
{
    return IsRegistryExplorerItem(item) ||
        IsJumpListExplorerItem(item) ||
        IsDetectItEasyItem(item) ||
        IsSimpleUnlockerItem(item) ||
        IsJournalTraceItem(item);
}

static DWORD GetPrimaryUrlOnlyTimeoutMs(const DownloadItem& item)
{
    if (IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item))
        return 900000;

    if (IsSimpleUnlockerItem(item))
        return 300000;

    return GetDirectDownloadTimeoutMs(item);
}

static std::vector<std::wstring> GetDirectDownloadUrls(const DownloadItem& item)
{
    std::vector<std::wstring> urls;
    AddUniqueString(urls, item.url);

    std::wstring name = ToLower(item.name);

    if (IsSystemInformerItem(item))
    {
        AddUniqueString(urls, L"https://github.com/winsiderss/systeminformer/releases/download/v3.2.25011.2103/systeminformer-3.2.25011-release-setup.exe");
        AddUniqueString(urls, L"https://downloads.sourceforge.net/project/systeminformer/systeminformer-3.2.25011-release-setup.exe");
        AddUniqueString(urls, L"https://sourceforge.net/projects/systeminformer/files/systeminformer-3.2.25011-release-setup.exe/download");
        AddUniqueString(urls, L"https://systeminformer.sourceforge.io/canary/systeminformer-3.2.25011-setup.exe");
    }
    else if (IsWinRarItem(item))
    {
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-722ru.exe");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-722.exe");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-721ru.exe");
        AddUniqueString(urls, L"https://www.rarlab.com/rar/winrar-x64-721.exe");
    }
    else if (IsSimpleUnlockerItem(item))
    {
        AddUniqueString(urls, L"https://simpleunlocker.ds1nc.ru/release/simpleunlocker_release.zip");
        AddUniqueString(urls, L"https://simpleunlocker.ds1nc.ru/release/simpleunlocker_release-u.zip");
    }
    else if (name.find(L"recuva") != std::wstring::npos)
    {
        AddUniqueString(urls, L"https://download.ccleaner.com/rcsetup154.exe");
        AddUniqueString(urls, L"https://download.ccleaner.com/recuva64.exe");
    }
    else if (name == L"everything")
    {
        AddUniqueString(urls, L"https://www.voidtools.com/Everything-1.5.0.1404a.x64.zip");
    }

    return urls;
}


enum class DownloadMethodChoice
{
    PowerShell,
    Curl,
    WinHttp,
    WinInet,
    Bits
};

static const wchar_t* GetDownloadMethodDisplayName(DownloadMethodChoice method)
{
    switch (method)
    {
    case DownloadMethodChoice::PowerShell:
        return L"PowerShell";
    case DownloadMethodChoice::Curl:
        return L"curl";
    case DownloadMethodChoice::WinHttp:
        return L"WinHTTP";
    case DownloadMethodChoice::WinInet:
        return L"WinINet";
    case DownloadMethodChoice::Bits:
        return L"BITS";
    }

    return L"загрузка";
}

static bool DownloadWithWinHttpValidated(
    const DownloadItem& item,
    const std::wstring& url,
    const std::wstring& targetPath,
    size_t linkIndex,
    size_t linkTotal)
{
    if (IsCancelRequested())
        return false;

    ClearDownloadedPayloadInvalidFlag();
    ClearTransferStalledFlag();
    g_lastDownloadMethod.clear();
    g_lastDownloadSpeedBps = 0;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    UpdateActiveDownloadStatus(item.name, L"WinHTTP", linkIndex, linkTotal, 0, 0);

    if (DownloadRecursive(url, targetPath, item.name, 0) &&
        ValidateDownloadedFileOrDelete(item, targetPath))
    {
        g_lastDownloadMethod = L"WinHTTP";
        UpdateActiveDownloadStatus(item.name, L"готово WinHTTP", linkIndex, linkTotal, GetFileSizeBytesOrZero(targetPath), g_lastDownloadSpeedBps);
        return true;
    }

    DebugLogLine(L"download", L"WinHTTP validated failed item=" + item.name +
        L" stalled=" + std::to_wstring(DidLastTransferStall() ? 1 : 0) +
        L" invalidPayload=" + std::to_wstring(DidLastDownloadedPayloadInvalid() ? 1 : 0) +
        L" targetSize=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath + L".part")));
    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    return false;
}

static bool DownloadWithBitsAdminFallback(
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    const std::wstring& itemName,
    size_t linkIndex,
    size_t linkTotal)
{
    ClearTransferStalledFlag();

    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";
    DeleteFileW(partPath.c_str());

    std::wstring bitsExe = GetSystemToolPath(L"bitsadmin.exe");
    std::wstring jobName = L"HolyCheck_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(linkIndex);

    std::wstring command =
        QuoteProcessArgument(bitsExe) +
        L" /transfer " + QuoteProcessArgument(jobName) +
        L" /download /priority FOREGROUND " +
        QuoteProcessArgument(url) + L" " + QuoteProcessArgument(partPath);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    const wchar_t* appName = FileExists(bitsExe) ? bitsExe.c_str() : nullptr;

    if (!CreateProcessW(appName, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        DebugLogLine(L"download", L"CreateProcess failed command=" + command + L" error=" + FormatWin32ErrorForLog(GetLastError()));
        return false;
    }

    UpdateActiveDownloadStatus(itemName, L"BITS", linkIndex, linkTotal, 0, 0);

    bool waited = WaitForProcessOrCancel(pi, timeoutMs);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    DebugLogLine(L"download", L"process exit command=" + command + L" exit=" + std::to_wstring(exitCode) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(partPath)));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!waited || exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
    {
        DeleteFileW(partPath.c_str());
        return false;
    }

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

    return true;
}

static bool DownloadBitsValidated(
    const DownloadItem& item,
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    size_t linkIndex,
    size_t linkTotal)
{
    if (IsCancelRequested())
        return false;

    ClearDownloadedPayloadInvalidFlag();
    ClearTransferStalledFlag();
    g_lastDownloadMethod.clear();
    g_lastDownloadSpeedBps = 0;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    if (DownloadWithBitsAdminFallback(url, targetPath, timeoutMs, item.name, linkIndex, linkTotal) &&
        ValidateDownloadedFileOrDelete(item, targetPath))
    {
        g_lastDownloadMethod = L"BITS";
        UpdateActiveDownloadStatus(item.name, L"готово BITS", linkIndex, linkTotal, GetFileSizeBytesOrZero(targetPath), g_lastDownloadSpeedBps);
        return true;
    }

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    return false;
}


static bool TryDirectDownloadCandidate(
    const DownloadItem& item,
    const std::wstring& url,
    const std::wstring& targetPath,
    DWORD timeoutMs,
    size_t linkIndex,
    size_t linkTotal,
    DownloadMethodChoice method,
    bool relaxedSpeedGuard = false)
{
    std::wstring methodName = GetDownloadMethodDisplayName(method);
    DebugLogLine(L"download", L"candidate start item=" + item.name +
        L" method=" + methodName + L" link=" + std::to_wstring(linkIndex) + L"/" + std::to_wstring(linkTotal) +
        L" timeout=" + std::to_wstring(timeoutMs) + L" relaxed=" + std::to_wstring(relaxedSpeedGuard ? 1 : 0) +
        L" target=" + targetPath + L" url=" + url);

    if (IsCancelRequested())
    {
        DebugLogLine(L"download", L"candidate cancelled before start item=" + item.name + L" method=" + methodName);
        return false;
    }

    ClearDownloadedPayloadInvalidFlag();
    ClearTransferStalledFlag();
    g_lastDownloadMethod.clear();
    g_lastDownloadSpeedBps = 0;
    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    bool githubUrl = IsGitHubUrl(url);
    bool ericLarge = IsEricZimmermanLargeItem(item) || IsEricZimmermanUrl(url);
    unsigned long long curlMinSpeed = relaxedSpeedGuard ? 0ull : GetMethodMinSpeedBps(item);
    DWORD curlLowSpeedWindowMs = relaxedSpeedGuard ? 0 : GetMethodLowSpeedWindowMs(item);
    DWORD curlNoProgressAbortMs = relaxedSpeedGuard ? 90000 : GetMethodNoProgressAbortMs(item);
    unsigned long long powerShellMinSpeed = relaxedSpeedGuard ? 0ull : GetPowerShellMinSpeedBps(item);
    DWORD powerShellLowSpeedWindowMs = relaxedSpeedGuard ? 0 : GetPowerShellLowSpeedWindowMs(item);
    DWORD powerShellNoProgressAbortMs = relaxedSpeedGuard ? 90000 : GetPowerShellNoProgressAbortMs(item);

    if (method == DownloadMethodChoice::WinHttp)
    {
        bool ok = DownloadWithWinHttpValidated(item, url, targetPath, linkIndex, linkTotal);
        DebugLogLine(L"download", L"candidate finish item=" + item.name + L" method=" + methodName +
            L" ok=" + std::to_wstring(ok ? 1 : 0) + L" stalled=" + std::to_wstring(DidLastTransferStall() ? 1 : 0) +
            L" invalidPayload=" + std::to_wstring(DidLastDownloadedPayloadInvalid() ? 1 : 0) +
            L" size=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
        return ok;
    }

    if (method == DownloadMethodChoice::WinInet)
    {
        UpdateActiveDownloadStatus(item.name, L"WinINet", linkIndex, linkTotal, 0, 0);

        if (DownloadWithWinInetFallback(
                url, targetPath, timeoutMs,
                relaxedSpeedGuard ? 0ull : GetMethodMinSpeedBps(item),
                relaxedSpeedGuard ? 0 : GetMethodLowSpeedWindowMs(item),
                relaxedSpeedGuard ? 90000 : GetMethodNoProgressAbortMs(item),
                item.name, linkIndex, linkTotal) &&
            ValidateDownloadedFileOrDelete(item, targetPath))
        {
            g_lastDownloadMethod = L"WinINet";
            UpdateActiveDownloadStatus(item.name, L"готово WinINet", linkIndex, linkTotal, GetFileSizeBytesOrZero(targetPath), g_lastDownloadSpeedBps);
            DebugLogLine(L"download", L"candidate success item=" + item.name + L" method=" + methodName + L" size=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
            return true;
        }
    }

    if (method == DownloadMethodChoice::Bits)
    {
        bool ok = DownloadBitsValidated(item, url, targetPath, timeoutMs, linkIndex, linkTotal);
        DebugLogLine(L"download", L"candidate finish item=" + item.name + L" method=" + methodName +
            L" ok=" + std::to_wstring(ok ? 1 : 0) + L" stalled=" + std::to_wstring(DidLastTransferStall() ? 1 : 0) +
            L" invalidPayload=" + std::to_wstring(DidLastDownloadedPayloadInvalid() ? 1 : 0) +
            L" size=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
        return ok;
    }

    if (method == DownloadMethodChoice::PowerShell)
    {
        UpdateActiveDownloadStatus(item.name, L"PowerShell", linkIndex, linkTotal, 0, 0);

        if (DownloadWithPowerShellFallback(
                url, targetPath, timeoutMs,
                powerShellMinSpeed,
                powerShellLowSpeedWindowMs,
                powerShellNoProgressAbortMs,
                item.name, linkIndex, linkTotal) &&
            ValidateDownloadedFileOrDelete(item, targetPath))
        {
            g_lastDownloadMethod = L"PowerShell";
            UpdateActiveDownloadStatus(item.name, L"готово PowerShell", linkIndex, linkTotal, GetFileSizeBytesOrZero(targetPath), g_lastDownloadSpeedBps);
            DebugLogLine(L"download", L"candidate success item=" + item.name + L" method=" + methodName + L" size=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
            return true;
        }
    }
    else
    {
        bool curlOk = false;
        UpdateActiveDownloadStatus(item.name, L"curl", linkIndex, linkTotal, 0, 0);

        if (githubUrl)
            curlOk = DownloadWithCurlGitHubFast(url, targetPath, timeoutMs, curlMinSpeed, curlLowSpeedWindowMs, curlNoProgressAbortMs, item.name, linkIndex, linkTotal);
        else if (ericLarge)
            curlOk = DownloadWithCurlBrowserLike(url, targetPath, timeoutMs, curlMinSpeed, curlLowSpeedWindowMs, curlNoProgressAbortMs, item.name, linkIndex, linkTotal);
        else
            curlOk = DownloadWithCurlFallback(url, targetPath, timeoutMs, curlMinSpeed, curlLowSpeedWindowMs, curlNoProgressAbortMs, item.name, linkIndex, linkTotal);

        if (curlOk && ValidateDownloadedFileOrDelete(item, targetPath))
        {
            g_lastDownloadMethod = L"curl";
            UpdateActiveDownloadStatus(item.name, L"готово curl", linkIndex, linkTotal, GetFileSizeBytesOrZero(targetPath), g_lastDownloadSpeedBps);
            DebugLogLine(L"download", L"candidate success item=" + item.name + L" method=" + methodName + L" size=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)));
            return true;
        }
    }

    DebugLogLine(L"download", L"candidate failed item=" + item.name + L" method=" + methodName +
        L" stalled=" + std::to_wstring(DidLastTransferStall() ? 1 : 0) +
        L" invalidPayload=" + std::to_wstring(DidLastDownloadedPayloadInvalid() ? 1 : 0) +
        L" targetSize=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath)) +
        L" partSize=" + std::to_wstring(GetFileSizeBytesOrZero(targetPath + L".part")));
    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    return false;
}


static std::vector<DownloadMethodChoice> GetOptimizedProblemMethods(const DownloadItem& item, const std::wstring& url)
{
    std::vector<DownloadMethodChoice> methods;

    if (IsGitHubUrl(url) || IsSystemInformerItem(item) || IsProcessHackerItem(item) ||
        IsBamParserItem(item) || IsJournalTraceItem(item))
    {
        methods.push_back(DownloadMethodChoice::WinInet);
        methods.push_back(DownloadMethodChoice::Curl);
        methods.push_back(DownloadMethodChoice::PowerShell);
        methods.push_back(DownloadMethodChoice::WinHttp);
    }
    else if (IsEricZimmermanUrl(url) || IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item) || IsSimpleUnlockerItem(item))
    {
        methods.push_back(DownloadMethodChoice::WinInet);
        methods.push_back(DownloadMethodChoice::PowerShell);
        methods.push_back(DownloadMethodChoice::Curl);
        methods.push_back(DownloadMethodChoice::WinHttp);
    }
    else if (ShouldTryPowerShellBeforeCurl(item))
    {
        methods.push_back(DownloadMethodChoice::Curl);
        methods.push_back(DownloadMethodChoice::WinInet);
        methods.push_back(DownloadMethodChoice::PowerShell);
        methods.push_back(DownloadMethodChoice::WinHttp);
    }
    else
    {
        methods.push_back(DownloadMethodChoice::Curl);
        methods.push_back(DownloadMethodChoice::WinInet);
        methods.push_back(DownloadMethodChoice::PowerShell);
        methods.push_back(DownloadMethodChoice::WinHttp);
    }

    std::vector<DownloadMethodChoice> uniqueMethods;
    for (DownloadMethodChoice method : methods)
    {
        if (std::find(uniqueMethods.begin(), uniqueMethods.end(), method) == uniqueMethods.end())
            uniqueMethods.push_back(method);
    }

    return uniqueMethods;
}

static bool DownloadPrimaryUrlOnlyValidated(const DownloadItem& item, const std::wstring& targetPath)
{
    if (IsCancelRequested())
        return false;

    DWORD timeoutMs = GetPrimaryUrlOnlyTimeoutMs(item);
    std::vector<std::wstring> urls = GetDirectDownloadUrls(item);
    DebugLogLine(L"download", L"direct download start item=" + item.name + L" target=" + targetPath +
        L" urls=" + std::to_wstring(urls.size()) + L" timeout=" + std::to_wstring(timeoutMs));

    auto tryOne = [&](size_t i, DownloadMethodChoice method, bool relaxedSpeedGuard = false) -> bool
    {
        if (IsCancelRequested())
            return false;

        return TryDirectDownloadCandidate(item, urls[i], targetPath, timeoutMs, i + 1, urls.size(), method, relaxedSpeedGuard);
    };

    if (IsProblemDownloadItem(item))
    {
        for (size_t i = 0; i < urls.size(); ++i)
        {
            std::vector<DownloadMethodChoice> methods = GetOptimizedProblemMethods(item, urls[i]);

            for (DownloadMethodChoice method : methods)
            {
                if (tryOne(i, method, false))
                    return true;

                if (IsCancelRequested())
                    return false;
            }
        }

        DebugLogLine(L"download", L"direct download failed item=" + item.name + L" all problem methods exhausted");
        return false;
    }

    auto tryAllLinksWithMethod = [&](DownloadMethodChoice method) -> bool
    {
        for (size_t i = 0; i < urls.size(); ++i)
        {
            if (IsCancelRequested())
                return false;

            if (tryOne(i, method, false))
                return true;
        }

        return false;
    };

    if (tryAllLinksWithMethod(DownloadMethodChoice::Curl))
        return true;

    if (tryAllLinksWithMethod(DownloadMethodChoice::WinInet))
        return true;

    bool ok = tryAllLinksWithMethod(DownloadMethodChoice::PowerShell);
    if (!ok)
        DebugLogLine(L"download", L"direct download failed item=" + item.name + L" all methods exhausted");
    return ok;
}

static void AddEricZimmermanKnownUrls(const DownloadItem& item, std::vector<std::wstring>& urls)
{
    AddUniqueString(urls, item.url);
}

static std::vector<std::wstring> GetEricZimmermanInstallUrls(const DownloadItem& item)
{
    std::vector<std::wstring> urls;
    AddEricZimmermanKnownUrls(item, urls);
    return urls;
}


static bool TryInstallDownloadUrl(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs)
{
    if (IsCancelRequested())
        return false;

    ClearDownloadedPayloadInvalidFlag();
    ClearTransferStalledFlag();
    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    if (DownloadWithCurlBrowserLike(url, targetPath, timeoutMs,
            0, 0, 0, item.name, 1, 1) &&
        ValidateDownloadedFileOrDelete(item, targetPath))
    {
        return true;
    }

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    return false;
}

static bool DownloadEricZimmermanLargeValidated(const DownloadItem& item, const std::wstring& targetPath)
{
    std::lock_guard<std::mutex> lock(g_ericDownloadMutex);

    const DWORD perUrlTimeoutMs = 900000;
    std::vector<std::wstring> urls = GetEricZimmermanInstallUrls(item);

    if (urls.empty())
    {
        PostLog(L"Eric: нет ссылок " + item.name);
        return false;
    }

    for (size_t i = 0; i < urls.size(); ++i)
    {
        if (IsCancelRequested())
            return false;

        PostLog(L"Eric " + std::to_wstring(i + 1) + L"/" + std::to_wstring(urls.size()) + L": " + item.name);

        if (TryInstallDownloadUrl(item, urls[i], targetPath, perUrlTimeoutMs))
            return true;
    }

    PostLog(L"Eric: установка не удалась " + item.name);
    return false;
}

