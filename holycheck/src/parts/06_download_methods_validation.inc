// 06_download_methods_validation.inc
// curl/PowerShell download methods, payload validation, Eric Zimmerman URLs.
// Original lines: 2857-3772

static bool DownloadWithCurlFallback(const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs)
{
    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";

    std::wstring curlExe = GetSystemToolPath(L"curl.exe");
    DWORD maxTimeSeconds = std::max<DWORD>(8, timeoutMs / 1000);
    DWORD connectTimeoutSeconds = timeoutMs >= 300000 ? 15 : 6;

    std::wstring command =
        QuoteProcessArgument(curlExe) +
        L" -L --fail --silent --show-error --http1.1 --ipv4 --ssl-no-revoke --tcp-nodelay"
        L" -C -"
        L" --retry 1 --retry-delay 1 --retry-connrefused"
        L" --speed-limit 2048 --speed-time 30"
        L" --connect-timeout " + std::to_wstring(connectTimeoutSeconds) +
        L" --max-time " + std::to_wstring(maxTimeSeconds) +
        L" -A " + QuoteProcessArgument(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36") +
        L" -H " + QuoteProcessArgument(L"Accept: application/octet-stream,*/*;q=0.8") +
        L" -H " + QuoteProcessArgument(L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6") +
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
        return false;

    ULONGLONG started = GetTickCount64();

    for (;;)
    {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 150);

        if (waitResult == WAIT_OBJECT_0)
            break;

        if (waitResult == WAIT_FAILED || IsCancelRequested() || GetTickCount64() - started > timeoutMs)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return false;

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

    return true;
}


static bool DownloadWithCurlBrowserLike(const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs)
{
    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";

    std::wstring curlExe = GetSystemToolPath(L"curl.exe");
    DWORD maxTimeSeconds = std::max<DWORD>(300, timeoutMs / 1000);

    // Browser-like large archive download. Do not use an aggressive speed-limit
    // here: Eric Zimmerman CDN can start slowly on some networks, and the old
    // 64 KB/s rule aborted valid downloads and then printed "direct links failed".
    // Keep IPv4 and ssl-no-revoke because they reduce common Windows curl stalls.
    std::wstring command =
        QuoteProcessArgument(curlExe) +
        L" -L --fail --silent --show-error --http1.1 --ipv4 --ssl-no-revoke --tcp-nodelay"
        L" -C -"
        L" --retry 3 --retry-delay 2 --retry-connrefused"
        L" --connect-timeout 15"
        L" --max-time " + std::to_wstring(maxTimeSeconds) +
        L" -A " + QuoteProcessArgument(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36") +
        L" -H " + QuoteProcessArgument(L"Accept: application/octet-stream,*/*;q=0.8") +
        L" -H " + QuoteProcessArgument(L"Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.7,en;q=0.6") +
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
        return false;

    ULONGLONG started = GetTickCount64();

    for (;;)
    {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 150);

        if (waitResult == WAIT_OBJECT_0)
            break;

        if (waitResult == WAIT_FAILED || IsCancelRequested() || GetTickCount64() - started > timeoutMs)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return false;

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

    return true;
}

static bool DownloadWithPowerShellFallback(const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs)
{
    if (IsCancelRequested())
        return false;

    std::wstring partPath = targetPath + L".part";
    DeleteFileW(partPath.c_str());

    std::wstring psUrl = EscapePowerShellSingleQuoted(url);
    std::wstring psPart = EscapePowerShellSingleQuoted(partPath);

    std::wstring script =
        L"$ProgressPreference='SilentlyContinue'; "
        L"[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
        L"$ErrorActionPreference='Stop'; "
        L"try { "
        L"$wc=New-Object Net.WebClient; "
        L"$wc.Headers.Add('User-Agent','Mozilla/5.0'); "
        L"$wc.DownloadFile('" + psUrl + L"','" + psPart + L"'); "
        L"exit 0 "
        L"} catch { "
        L"try { "
        L"Invoke-WebRequest -UseBasicParsing -Headers @{'User-Agent'='Mozilla/5.0'} -Uri '" + psUrl + L"' -OutFile '" + psPart + L"' -TimeoutSec 10; "
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
        return false;

    ULONGLONG started = GetTickCount64();

    for (;;)
    {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 150);

        if (waitResult == WAIT_OBJECT_0)
            break;

        if (waitResult == WAIT_FAILED || IsCancelRequested() || GetTickCount64() - started > timeoutMs)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !FileExists(partPath) || IsCancelRequested())
        return false;

    DeleteFileW(targetPath.c_str());

    if (!MoveFileW(partPath.c_str(), targetPath.c_str()))
        return false;

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

static std::wstring FindLaunchPathForItem(const DownloadItem& item, const std::wstring& baseFolder)
{
    std::wstring target = GetItemTargetPath(item, baseFolder);

    if (EndsWithI(target, L".zip"))
    {
        std::wstring extractDir = GetItemExtractDir(item, baseFolder);
        std::wstring exePath;

        if (FindFirstExeRecursive(extractDir, exePath))
            return exePath;

        // ZIP сам по себе не открываем как программу: если он уже скачан,
        // одиночный клик сначала распакует архив через ProcessItem().
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

static bool ShouldOpenItemElevated(const DownloadItem& item)
{
    std::wstring name = ToLower(item.name);

    return name.find(L"registryexplorer") != std::wstring::npos ||
        (name.find(L"registry") != std::wstring::npos && name.find(L"explorer") != std::wstring::npos) ||
        name.find(L"recuva") != std::wstring::npos;
}

static bool OpenItemPath(const DownloadItem& item, const std::wstring& path)
{
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
    if (CopyTextToClipboard(text))
        PostLog(L"Скопировано: " + name);
    else
        PostLog(L"Не скопировано: " + name);
}

static const wchar_t* EVERYTHING_QUERY_1 = LR"HCEV1(ext:.exe;.jar;.zip;.rar regex:(impact|wurst|bleach[-_]?hack|aristois|huzuni|skill[-_]?client|nodus|inertia|ares|sigma|meteor|BetterHitReg|atomic|zamorozka|liquid[-_]?bounce|nurik|nursultan|celestial|calestial|celka|expensive|neverhook|excellent|wexside|wild|minced|deadcode|akrien|future|dreampool|vape|infinity|squad|no[-_]?rules|konas|zeus[-_]?client|rich[-_]?client|ghost[-_]?client|rusher[-_]?hack|thunder[-_]?hack|moon[-_]?hack|winner|nova|exire|doomsday|nightware|ricardo|extazyy|troxill|arbuz|dauntiblyat|rename[-_]?me[-_]?please|edit[-_]?me|takker|faker|xameleon|fuze[-_]?client|wise[-_]?folder|net[-_]?limiter|feather|delta|eclipse|venus|jex|hakari|hush|hach|rogalik|catlavan|haruka|wissend|fluger|sperma|vortex|newcode|astra|britva|bariton|bot|player|freecam|bedrock|hotbar|swap|chest|gumball|tweak|entity|crystal|lowdurabilityswitcher|optimizer|viabackwards|viaforge|viaproxy|hitbox|elytra|through|mob|auto|place|health|inventory|x[-_]?ray|clean[-_]?cut|smart[-_]?moving|save[-_]?searcher|world[-_]?downloader|trade[-_]?finder|chorus[-_]?find|inv[-_]?move|chunk[-_]?copy|seed[-_]?cracker|diamond[-_]?sim|forge[-_]?hax|step[-_]?up|client[-_]?commands|camera[-_]?utils|cheat[-_]?utils|universal[-_]?mod|swing[-_]?through[-_]?grass))HCEV1";

static const wchar_t* EVERYTHING_QUERY_2 = LR"HCEV2(ext:.txt;.json;.toml;.yml;.cfg;.properties;.ini | folder: dm:last14days regex:(shellbag|bariton|bot|player|freecam|bedrock|hotbar|swap|chest|gumball|tweak|entity|crystal|optimizer|viabackwards|viaforge|viaproxy|hitbox|elytra|through|mob|auto|place|health|inventory|x[-_]?ray|clean[-_]?cut|smart[-_]?moving|save[-_]?searcher|world[-_]?downloader|trade[-_]?finder|chorus[-_]?find|inv[-_]?move|chunk[-_]?copy|seed[-_]?cracker|diamond[-_]?sim|forge[-_]?hax|step[-_]?up|client[-_]?commands|camera[-_]?utils|cheat[-_]?utils|universal[-_]?mod|swing[-_]?through[-_]?grass))HCEV2";

// Everything and PowerShell helper text files are no longer created on disk.
// Texts are available inside the utility via copy buttons.


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

static const wchar_t* POWERSHELL_TXT = LR"HCPOWERSHELL($ErrorActionPreference="SilentlyContinue";$out="$PWD\check.txt";$twinksOut="$PWD\checktwinks.txt";$mac = (Get-NetAdapter -Physical | Where-Object {$_.Status -eq 'Up'} | Sort-Object -Property InterfaceIndex | Select-Object -First 1).MacAddress;if (-not $mac) { $mac = (Get-NetAdapter | Where-Object {$_.Status -eq 'Up' -and $_.InterfaceDescription -notmatch 'Virtual|WireGuard|TAP'} | Sort-Object -Property InterfaceIndex | Select-Object -First 1).MacAddress };"1. Модель системы: $((Get-WmiObject Win32_ComputerSystem).Model)`n2. Серийный BIOS: $((Get-WmiObject Win32_BIOS).SerialNumber)`n3. Реестр: $((Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS').SystemProductName)`n4. Модель диска: $((Get-WmiObject Win32_DiskDrive | ForEach-Object { "$($_.Model) ($([math]::Round($_.Size/1GB, 2)) GB)" }) -join ', ')`n5. Видеоадаптеры: $(($(Get-CimInstance Win32_PnPSignedDriver | Where-Object {$_.DeviceClass -eq 'DISPLAY'} | ForEach-Object { "$($_.DeviceName) | $($_.Manufacturer) | $($_.DriverProviderName)" }) -join '; '))`n6. Виртуальные устройства: $((Get-WmiObject Win32_PnPEntity | Where-Object {$_.Name -match 'VMware|Virtual|VBox|Hyper-V|QEMU|Xen|KVM|VirtIO|VirtualBox|VMCI|SVGA|VMBus'}).Name -join ', ')`n7. Mac: $mac">$out;$bootTime=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;function f($t){$p=(' '*((45-$t.Length)/2));"${p}${t}${p}"+(' '*(45-$p.Length*2-$t.Length))};"`n`n=============================================`n$(f 'ИНФОРМАЦИЯ О ВКЛЮЧЕНИИ ПК')`n=============================================`nПоследнее включение ПК: $($bootTime.ToString('yyyy-MM-dd HH:mm:ss'))">>$out;"`n`n`n=============================================`n$(f 'СОСТОЯНИЕ СЛУЖБ')`n=============================================">>$out;$services=@("PcaSvc","DPS","SysMain","EventLog","bam")|%{$s=Get-Service $_ -ea 0;if($s){$t=(Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$_" -n Start -ea 0).Start;$n=switch($t){2{'Авто'}3{'Вручную'}4{'Отключена'}default{'Неизвестно'}};$p=if($s.Status-eq'Running'){try{$procId=(Get-WmiObject Win32_Service -Filter "Name='$_'").ProcessID;(Get-Process -Id $procId -ea 0).StartTime.ToString('yyyy-MM-dd HH:mm:ss')}catch{'Не удалось получить'}}else{'Не запущена'};[PSCustomObject]@{'Служба'=$_.PadRight(8);'Состояние'=$s.Status.ToString().PadRight(10);'Тип запуска'=$n.PadRight(9);'Время запуска'=$p.PadRight(20)}}};($services|ft -AutoSize -Wrap|Out-String -Width 4096|%{$_.Trim()})>>$out;"`n`n`n=============================================`n$(f 'СОБЫТИЯ ОЧИСТКИ ЖУРНАЛОВ (ID 104)')`n=============================================">>$out;$e=Get-WinEvent -LogName System -FilterXPath "*[System[(EventID=104)]]" -ea 0;if($e){$e|%{$d=($_.Message-split"`n")[0];"ID $($_.Id) | $($_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')) | $d">>$out}};"`n`n`n=============================================`n$(f 'СОБЫТИЯ ПРИЛОЖЕНИЯ (ID 3079)')`n=============================================">>$out;Get-WinEvent -LogName Application -FilterXPath "*[System[(EventID=3079)]]" -MaxEvents 5 -ea 0|%{$m=($_.Message-split"Подробности:")[-1].Trim()-replace"`n|`r|\s+"," ";"ID $($_.Id) | $($_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')) | $m">>$out};"`n`n`n=============================================`n$(f 'СПИСОК ТВИНКОВ ИГРОКА')`n=============================================">>$out;$r=@{};$err=@();Get-ChildItem -Path . -Recurse -Include *.log,*.log.gz -ea 0|%{try{if($_.Extension-eq'.gz'){$c=[System.IO.Compression.GzipStream]::new([System.IO.File]::OpenRead($_.FullName),[System.IO.Compression.CompressionMode]::Decompress);$s=[System.IO.StreamReader]::new($c);while(-not$s.EndOfStream){$l=$s.ReadLine();if($l-match'setting user:\s*(.+)$'){$r[$matches[1].Trim()]=1}};$s.Close()}else{Get-Content $_.FullName|?{$_-match'setting user:\s*(.+)$'}|%{$r[$matches[1].Trim()]=1}}}catch{$err+="Ошибка логов $($_.Name): $_"}};if(Test-Path "$env:APPDATA\.iasx"){try{[System.Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes("$env:APPDATA\.iasx")) -split '[\0-\x1F\x7F-\x9F]'|%{$_.TrimEnd('t')}|?{$_-match '^[\p{IsCyrillic}\p{IsBasicLatin}_]{3,20}$'-and$_-notmatch'=$'-and$_-notmatch'(java|alias|user|pass|field|object|count|list|data|xp|sr|lastused|premium|obj|q$|\[)'}|%{$r[$_]=1}}catch{$err+="Ошибка appdata iasx: $_"}};Get-ChildItem -Path . -Recurse -Filter *.iasx -ea 0|%{try{[System.Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($_.FullName)) -split '[\0-\x1F\x7F-\x9F]'|%{$_.TrimEnd('t')}|?{$_-match '^[\p{IsCyrillic}\p{IsBasicLatin}_]{3,20}$'-and$_-notmatch'=$'-and$_-notmatch'(java|alias|user|pass|field|object|count|list|data|xp|sr|lastused|premium|obj|q$|\[)'}|%{$r[$_]=1}}catch{$err+="Ошибка iasx $($_.Name): $_"}};Get-ChildItem -Path . -Recurse -Filter accounts_v1.do_not_send_to_anyone -ea 0|%{try{$b=[System.IO.File]::ReadAllBytes($_.FullName);$ms=New-Object System.IO.MemoryStream(,$b[2..($b.Length-1)]);$ds=New-Object System.IO.Compression.DeflateStream($ms,[System.IO.Compression.CompressionMode]::Decompress);$sr=New-Object System.IO.StreamReader($ds);$c=$sr.ReadToEnd();$sr.Close();$ds.Close();$ms.Close();$c-split'[\x00-\x1F]'|?{$_-match'^[a-zA-Z0-9_]{3,}$'}|%{$r[$_]=1}}catch{$err+="Ошибка accounts_v1 $($_.Name): $_"}};if(Test-Path "$PWD\usercache.json"){try{Get-Content "$PWD\usercache.json" -Raw|ConvertFrom-Json|Select-Object -ExpandProperty name|%{$r[$_]=1}}catch{$err+="Ошибка usercache.json: $_"}};if(Test-Path "$PWD\TlauncherProfiles.json"){try{Get-Content "$PWD\TlauncherProfiles.json" -Raw|ConvertFrom-Json|%{$_.accounts.PSObject.Properties.Value|Select-Object -Expand username}|%{$r[$_]=1}}catch{$err+="Ошибка TlauncherProfiles.json: $_"}};if(Test-Path "$PWD\LabyMod\accounts.json"){try{Get-Content "$PWD\LabyMod\accounts.json" -Raw|ConvertFrom-Json|%{$_.accounts.PSObject.Properties.Value.username}|%{$r[$_]=1}}catch{$err+="Ошибка LabyMod/accounts.json: $_"}};if(Test-Path "$PWD\labymod-neo\accounts.json"){try{Get-Content "$PWD\labymod-neo\accounts.json" -Raw|ConvertFrom-Json|%{$_.accounts.PSObject.Properties.Value.username}|%{$r[$_]=1}}catch{$err+="Ошибка labymod-neo/accounts.json: $_"}};Get-ChildItem -Path . -Recurse -Filter ias.json -ea 0|%{try{$j=Get-Content $_.FullName -Raw|ConvertFrom-Json;if($j.accounts){$j.accounts|%{if($_.name){$r[$_.name]=1}elseif($_.data.username){$r[$_.data.username]=1}}}}catch{$err+="Ошибка ias.json $($_.Name): $_"}};if(Test-Path "$PWD\usernamecache.json"){try{Get-Content "$PWD\usernamecache.json" -Raw|ConvertFrom-Json|%{$_.PSObject.Properties.Value}|%{$r[$_]=1}}catch{$err+="Ошибка usernamecache.json: $_"}};if(Test-Path "$env:APPDATA\.lunarclient"){try{Get-ChildItem "$env:APPDATA\.lunarclient" -Recurse -Filter *.json -ea 0|%{try{$c=Get-Content $_.FullName -Raw;if($c -match'"username"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}if($c -match'"displayName"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}}catch{$err+="Ошибка Lunar Client $($_.Name): $_"}}}catch{$err+="Ошибка доступа к Lunar Client: $_"}};if(Test-Path "$env:APPDATA\lunarclient"){try{Get-ChildItem "$env:APPDATA\lunarclient" -Recurse -Filter *.json -ea 0|%{try{$c=Get-Content $_.FullName -Raw;if($c -match'"username"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}if($c -match'"displayName"\s*:\s*"([^"]+)"'){$matches[1]|%{$r[$_]=1}}}catch{$err+="Ошибка Lunar Client $($_.Name): $_"}}}catch{$err+="Ошибка доступа к Lunar Client: $_"}};if($r.Count-gt0){($r.Keys-join' ')>$twinksOut;$r.Keys>>$out}else{"Нет данных о твинках">>$out};if($err){"Ошибки:`n$($err-join"`n")">>$out}
)HCPOWERSHELL";

static const wchar_t* POWERSHELL_SITES_TXT = LR"HCSITES($sites="anticheat.ac","mods.holyworld.me","github.com","voidtools.com","privazer.com","nirsoft.net","sourceforge.net","download.ericzimmermanstools.com","win-rar.com","simpleunlocker.ds1nc.ru"; function Test-Site($u){ $r=@(); if(Select-String -Path "$env:SystemRoot\System32\drivers\etc\hosts" -Pattern "(?mi)^\s*?\d.+?$($u.Replace('.','\.'))\s*$"){$r+="HOSTS"}; try{if((Resolve-DnsName $u -ErrorAction Stop -Type A|% IPAddress)-like'127.*'){$r+="DNS_LOOPBACK"}}catch{}; $regPath="HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings\ZoneMap\Domains\$($u.Replace('.','\\'))";if(Test-Path $regPath){$r+="REGISTRY_ZONE"}elseif(Test-Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings\ZoneMap\Domains\$u"){$r+="REGISTRY_ZONE"}; if(Get-NetFirewallRule|?{$_.DisplayName-like"*$u*"-and$_.Action-eq"Block"}){$r+="FIREWALL"}; try{iwr "https://$u"-TimeoutSec 5 -UseBasicParsing -DisableKeepAlive -ErrorAction Stop|Out-Null}catch{if($_.Exception.Response.StatusCode-eq407){$r+="PROXY_BLOCK"}elseif(-not$_.Exception.Response){try{iwr "http://$u"-TimeoutSec 3 -UseBasicParsing -DisableKeepAlive -ErrorAction Stop|Out-Null}catch{$r+="WEB_BLOCKED"}}}; $tcp80=Test-NetConnection $u -Port 80 -WarningAction SilentlyContinue; $tcp443=Test-NetConnection $u -Port 443 -WarningAction SilentlyContinue; if(-not$tcp80.TcpTestSucceeded-and$u-ne'anticheat.ac'){$r+="PORT_80_BLOCKED"};if(-not$tcp443.TcpTestSucceeded-and$u-ne'anticheat.ac'){$r+="PORT_443_BLOCKED"}; if($r.Count-gt0){"$u → БЛОКИРОВКА: $($r-join', ')"}else{"$u → OK"}}; $sites|%{Test-Site $_}
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

    // End of Central Directory signature. This catches most truncated ZIP/JAR files
    // without extracting the archive.
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

    // Some ZIP64 archives can be unusual; still require a central-directory marker.
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

    if (EndsWithI(fileLowerForSize, L".exe") && size.QuadPart < 4096)
        return false;

    if ((EndsWithI(fileLowerForSize, L".zip") || EndsWithI(fileLowerForSize, L".jar")) && size.QuadPart < 22)
        return false;

    // Do not hardcode a large minimum size for Eric Zimmerman GUI tools.
    // Their package sizes can change between releases. A valid ZIP signature
    // plus non-HTML payload is a better generic validation here; a too-large
    // minimum caused real downloads to be deleted as "invalid".

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
    if (IsValidDownloadedPayload(item, targetPath))
        return true;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    PostLog(L"Некорректный файл: " + item.name);
    return false;
}

static bool DownloadRecursiveValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath)
{
    if (!DownloadRecursive(url, targetPath, item.name, 0))
        return false;

    return ValidateDownloadedFileOrDelete(item, targetPath);
}

static bool DownloadCurlValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath)
{
    if (!DownloadWithCurlFallback(url, targetPath, GetPowerShellTimeoutMs(item)))
        return false;

    return ValidateDownloadedFileOrDelete(item, targetPath);
}

static bool DownloadPowerShellValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath)
{
    if (!DownloadWithPowerShellFallback(url, targetPath, GetPowerShellTimeoutMs(item)))
        return false;

    return ValidateDownloadedFileOrDelete(item, targetPath);
}

static bool IsPrimaryUrlOnlyItem(const DownloadItem& item)
{
    return IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item) || IsSimpleUnlockerItem(item);
}

static DWORD GetPrimaryUrlOnlyTimeoutMs(const DownloadItem& item)
{
    if (IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item))
        return 180000;

    if (IsSimpleUnlockerItem(item))
        return 60000;

    return GetPowerShellTimeoutMs(item);
}

static bool DownloadPrimaryUrlOnlyValidated(const DownloadItem& item, const std::wstring& targetPath)
{
    if (IsCancelRequested())
        return false;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    DWORD timeoutMs = GetPrimaryUrlOnlyTimeoutMs(item);

    PostLog(L"Прямая ссылка: " + item.name);

    if (IsRegistryExplorerItem(item) || IsJumpListExplorerItem(item))
    {
        if (DownloadWithCurlBrowserLike(item.url, targetPath, timeoutMs) &&
            ValidateDownloadedFileOrDelete(item, targetPath))
        {
            return true;
        }
    }
    else
    {
        if (DownloadWithCurlFallback(item.url, targetPath, timeoutMs) &&
            ValidateDownloadedFileOrDelete(item, targetPath))
        {
            return true;
        }
    }

    if (IsCancelRequested())
        return false;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    if (DownloadWithPowerShellFallback(item.url, targetPath, timeoutMs) &&
        ValidateDownloadedFileOrDelete(item, targetPath))
    {
        return true;
    }

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());
    return false;
}


static void AddEricZimmermanKnownUrls(const DownloadItem& item, std::vector<std::wstring>& urls)
{
    if (IsRegistryExplorerItem(item))
    {
        AddUniqueString(urls, L"https://download.ericzimmermanstools.com/net9/RegistryExplorer.zip");
        AddUniqueString(urls, L"https://f001.backblazeb2.com/file/EricZimmermanTools/net9/RegistryExplorer.zip");
        AddUniqueString(urls, L"https://f001.backblazeb2.com/file/EricZimmermanTools/RegistryExplorer.zip");
    }
    else if (IsJumpListExplorerItem(item))
    {
        AddUniqueString(urls, L"https://download.ericzimmermanstools.com/net9/JumpListExplorer.zip");
        AddUniqueString(urls, L"https://f001.backblazeb2.com/file/EricZimmermanTools/net9/JumpListExplorer.zip");
        AddUniqueString(urls, L"https://f001.backblazeb2.com/file/EricZimmermanTools/JumpListExplorer.zip");
    }
}

static std::vector<std::wstring> GetEricZimmermanInstallUrls(const DownloadItem& item)
{
    std::vector<std::wstring> urls;

    // First try to discover the current official link from the live index page.
    // The file names and versions may change; the official page is the source of
    // truth and also avoids stale hardcoded direct URLs.
    std::vector<unsigned char> html;
    const std::wstring home = L"https://ericzimmerman.github.io/";

    if (FetchUrlBytes(home, item.name, html))
    {
        std::wstring discovered = FindBestDownloadLinkForItem(home, html, item);
        if (!discovered.empty())
            AddUniqueString(urls, discovered);
    }

    AddEricZimmermanKnownUrls(item, urls);
    return urls;
}

static bool TryInstallDownloadUrl(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs)
{
    if (IsCancelRequested())
        return false;

    DeleteFileW(targetPath.c_str());
    DeleteFileW((targetPath + L".part").c_str());

    // 1) Native WinHTTP. This keeps the utility independent of curl/PowerShell.
    if (DownloadRecursiveValidated(item, url, targetPath))
        return true;

    if (IsCancelRequested())
        return false;

    // 2) Windows curl. This behaves closest to a manual browser/CDN download.
    if (DownloadWithCurlBrowserLike(url, targetPath, timeoutMs) &&
        ValidateDownloadedFileOrDelete(item, targetPath))
    {
        return true;
    }

    if (IsCancelRequested())
        return false;

    // 3) PowerShell fallback. This is slower, but useful on systems where WinHTTP
    // or curl is blocked/missing. It is intentionally kept last.
    if (DownloadWithPowerShellFallback(url, targetPath, timeoutMs) &&
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

