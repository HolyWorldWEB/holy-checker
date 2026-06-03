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


static bool IsRunningAsAdministrator()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
        &ntAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup))
    {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin == TRUE;
}

static std::wstring QuoteShellArgument(const std::wstring& arg)
{
    if (arg.empty())
        return L"\"\"";

    bool needsQuotes = arg.find_first_of(L" \t\"") != std::wstring::npos;

    if (!needsQuotes)
        return arg;

    std::wstring out = L"\"";
    size_t slashCount = 0;

    for (wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++slashCount;
            continue;
        }

        if (ch == L'\"')
        {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(ch);
            slashCount = 0;
            continue;
        }

        if (slashCount > 0)
        {
            out.append(slashCount, L'\\');
            slashCount = 0;
        }

        out.push_back(ch);
    }

    if (slashCount > 0)
        out.append(slashCount * 2, L'\\');

    out.push_back(L'\"');
    return out;
}

static std::wstring BuildRelaunchParameters()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (!argv)
        return L"";

    std::wstring params;

    for (int i = 1; i < argc; ++i)
    {
        if (!params.empty())
            params += L" ";

        params += QuoteShellArgument(argv[i]);
    }

    LocalFree(argv);
    return params;
}

static bool RelaunchAsAdministrator()
{
    wchar_t exePath[MAX_PATH]{};

    if (!GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>((sizeof(exePath) / sizeof(exePath[0])))))
        return false;

    std::wstring params = BuildRelaunchParameters();

    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"runas",
        exePath,
        params.empty() ? nullptr : params.c_str(),
        nullptr,
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(result) > 32;
}

static std::wstring ToLowerCopy(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
    {
        return static_cast<wchar_t>(towlower(ch));
    });

    return value;
}

static std::wstring TrimXmlText(std::wstring value)
{
    for (wchar_t& ch : value)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            ch = L' ';
    }

    while (!value.empty() && iswspace(value.front()))
        value.erase(value.begin());

    while (!value.empty() && iswspace(value.back()))
        value.pop_back();

    return value;
}

static std::wstring ExtractFileNameOnly(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");

    if (pos == std::wstring::npos || pos + 1 >= path.size())
        return path;

    return path.substr(pos + 1);
}

static bool IsLikelySystemExecutable(const std::wstring& path)
{
    std::wstring lower = ToLowerCopy(path);
    std::replace(lower.begin(), lower.end(), L'/', L'\\');
    std::wstring file = ExtractFileNameOnly(lower);

    if (lower.empty())
        return true;

    wchar_t winDirBuf[MAX_PATH * 4]{};
    std::wstring winDir;
    UINT winLen = GetWindowsDirectoryW(winDirBuf, ARRAYSIZE(winDirBuf));
    if (winLen > 0 && winLen < ARRAYSIZE(winDirBuf))
    {
        winDir = ToLowerCopy(winDirBuf);
        std::replace(winDir.begin(), winDir.end(), L'/', L'\\');
        while (!winDir.empty() && winDir.back() == L'\\')
            winDir.pop_back();
    }

    std::wstring winLeafNeedle;
    if (!winDir.empty())
        winLeafNeedle = L"\\" + ExtractFileNameOnly(winDir) + L"\\";

    auto has = [&](const wchar_t* needle) -> bool
    {
        return lower.find(needle) != std::wstring::npos;
    };

    // Самый надёжный быстрый фильтр для этой утилиты: не хешировать каждый exe,
    // а не пропускать Windows/SystemRoot/WinSxS/WindowsApps/Microsoft системные пути.
    // Хеши без локальной базы эталонных системных хешей не дают 100% результата и сильно тормозят скан.
    if ((!winDir.empty() && (lower.rfind(winDir + L"\\", 0) == 0 || (!winLeafNeedle.empty() && lower.find(winLeafNeedle) != std::wstring::npos))) ||
        has(L"\\windows\\") ||
        has(L"\\systemroot\\") ||
        has(L"\\winsxs\\") ||
        has(L"\\system32\\") ||
        has(L"\\syswow64\\") ||
        has(L"\\microsoft.net\\framework") ||
        has(L"\\$windows.~bt\\") ||
        has(L"\\$windows.~ws\\") ||
        has(L"\\program files\\windowsapps\\") ||
        has(L"\\program files (x86)\\windowsapps\\") ||
        has(L"\\windowsapps\\microsoft.") ||
        has(L"\\windowsapps\\windows.") ||
        has(L"\\programdata\\microsoft\\windows\\") ||
        has(L"\\microsoft\\windows\\") ||
        has(L"microsoft.windows.") ||
        has(L"windows.client.") ||
        has(L"startmenuexperiencehost") ||
        has(L"client.cbs") ||
        has(L"_cw5n1h2txyewy") ||
        has(L"_8wekyb3d8bbwe") ||
        has(L"__8wekyb3d8bbwe"))
    {
        return true;
    }

    if (lower.rfind(L"c:\\windows\\", 0) == 0 ||
        lower.rfind(L"c:\\programdata\\microsoft\\windows\\", 0) == 0 ||
        (lower.rfind(L"\\device\\harddiskvolume", 0) == 0 && lower.find(L"\\windows\\") != std::wstring::npos))
    {
        return true;
    }

    static const wchar_t* systemNames[] =
    {
        L"svchost.exe", L"conhost.exe", L"dllhost.exe", L"rundll32.exe", L"regsvr32.exe",
        L"taskhostw.exe", L"taskhost.exe", L"taskeng.exe", L"explorer.exe", L"sihost.exe",
        L"searchindexer.exe", L"searchhost.exe", L"searchprotocolhost.exe", L"runtimebroker.exe",
        L"audiodg.exe", L"csrss.exe", L"winlogon.exe", L"services.exe", L"lsass.exe",
        L"smss.exe", L"fontdrvhost.exe", L"wmiprvse.exe", L"werfault.exe", L"wermgr.exe",
        L"dwm.exe", L"ctfmon.exe", L"securityhealthsystray.exe", L"startmenuexperiencehost.exe",
        L"shellexperiencehost.exe", L"applicationframehost.exe", L"lockapp.exe", L"textinputhost.exe",
        L"backgroundtaskhost.exe", L"systemsettings.exe", L"trustedinstaller.exe", L"tiworker.exe",
        L"mousocoreworker.exe", L"wuauclt.exe", L"musnotification.exe", L"musnotificationux.exe",
        L"usocoreworker.exe", L"compattelrunner.exe", L"devicecensus.exe", L"consent.exe",
        L"smartscreen.exe", L"securityhealthservice.exe", L"securityhealthhost.exe",
        L"sppsvc.exe", L"vssvc.exe", L"spoolsv.exe", L"wscript.exe", L"cscript.exe",
        L"msiexec.exe", L"msedgewebview2.exe", L"client.cbs", L"searchapp.exe",
        L"widgets.exe", L"widgetservice.exe", L"onedrivesetup.exe", L"setuphost.exe",
        L"backgrounddownload.exe", L"aggregatorhost.exe", L"systemsettingsbroker.exe",
        L"yourphone.exe", L"phoneexperiencehost.exe", L"gamebar.exe", L"gamebarftserver.exe",
        L"gamebarpresencewriter.exe", L"xboxgamebarwidgets.exe"
    };

    for (const wchar_t* name : systemNames)
    {
        std::wstring n = ToLowerCopy(name);
        if (file == n || file.find(n) != std::wstring::npos)
            return true;
    }

    // Дополнительные системные/служебные зоны. Это быстрее и надёжнее для live-скана,
    // чем хешировать каждый exe без локальной базы эталонных системных хешей.
    if (has(L"\\program files\\microsoft\\") ||
        has(L"\\program files (x86)\\microsoft\\") ||
        has(L"\\program files\\windows defender\\") ||
        has(L"\\program files\\windows security\\") ||
        has(L"\\program files\\windows nt\\") ||
        has(L"\\program files\\internet explorer\\") ||
        has(L"\\program files (x86)\\internet explorer\\") ||
        has(L"\\program files\\common files\\microsoft shared\\") ||
        has(L"\\program files (x86)\\common files\\microsoft shared\\") ||
        has(L"\\appdata\\local\\microsoft\\windows\\") ||
        has(L"\\appdata\\roaming\\microsoft\\windows\\") ||
        has(L"\\appdata\\local\\packages\\microsoft.") ||
        has(L"\\appdata\\local\\packages\\windows."))
    {
        return true;
    }

    // Prefetch-файлы часто дают только имя exe. Если это типичное системное имя — не выводим вообще.
    if (lower.find(L"\\") == std::wstring::npos && lower.find(L"/") == std::wstring::npos)
    {
        if (file.rfind(L"microsoft.", 0) == 0 || file.rfind(L"windows.", 0) == 0 ||
            file.find(L"setup") != std::wstring::npos || file.find(L"update") != std::wstring::npos)
            return true;
    }

    return false;
}

static std::wstring DevicePathToDosPath(const std::wstring& path)
{
    if (path.empty() || path[0] != L'\\')
        return path;

    wchar_t drives[512]{};
    DWORD len = GetLogicalDriveStringsW(static_cast<DWORD>((sizeof(drives) / sizeof(drives[0])) - 1), drives);

    if (len == 0 || len >= (sizeof(drives) / sizeof(drives[0])))
        return path;

    for (wchar_t* drive = drives; *drive; drive += wcslen(drive) + 1)
    {
        wchar_t letter[] = { drive[0], L':', L'\0' };
        wchar_t deviceName[1024]{};

        if (!QueryDosDeviceW(letter, deviceName, static_cast<DWORD>((sizeof(deviceName) / sizeof(deviceName[0])))))
            continue;

        std::wstring device = deviceName;

        if (path.size() > device.size() && _wcsnicmp(path.c_str(), device.c_str(), device.size()) == 0 && path[device.size()] == L'\\')
        {
            return std::wstring(letter) + path.substr(device.size());
        }
    }

    return path;
}

static bool IsPlausibleFileTime(const FILETIME& ft)
{
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;

    if (value.QuadPart == 0)
        return false;

    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st))
        return false;

    return st.wYear >= 2000 && st.wYear <= 2100;
}

static bool TryReadBamFileTime(const BYTE* data, DWORD dataLen, FILETIME& out)
{
    if (!data || dataLen < sizeof(FILETIME))
        return false;

    const DWORD offsets[] = { 0, 8, 16 };

    for (DWORD offset : offsets)
    {
        if (dataLen < offset + sizeof(FILETIME))
            continue;

        FILETIME ft{};
        std::memcpy(&ft, data + offset, sizeof(FILETIME));

        if (IsPlausibleFileTime(ft))
        {
            out = ft;
            return true;
        }
    }

    return false;
}

static std::wstring FileTimeToLocalText(const FILETIME& ft)
{
    SYSTEMTIME utc{};
    SYSTEMTIME local{};

    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return L"дата неизвестна";

    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%02u.%02u.%04u %02u:%02u:%02u",
        local.wDay,
        local.wMonth,
        local.wYear,
        local.wHour,
        local.wMinute,
        local.wSecond
    );

    return buffer;
}

static bool IsoUtcTextToLocalText(const std::wstring& iso, std::wstring& out)
{
    if (iso.size() < 19)
        return false;

    SYSTEMTIME st{};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

    if (swscanf_s(iso.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return false;

    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = static_cast<WORD>(second);

    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft))
        return false;

    out = FileTimeToLocalText(ft);
    return true;
}

static std::wstring ExtractXmlAttribute(const std::wstring& xml, const std::wstring& tag, const std::wstring& attr)
{
    size_t tagPos = xml.find(L"<" + tag);

    if (tagPos == std::wstring::npos)
        return L"";

    size_t tagEnd = xml.find(L'>', tagPos);

    if (tagEnd == std::wstring::npos)
        tagEnd = xml.size();

    std::wstring key = attr + L"=";
    size_t attrPos = xml.find(key, tagPos);

    if (attrPos == std::wstring::npos || attrPos > tagEnd)
        return L"";

    attrPos += key.size();

    if (attrPos >= xml.size())
        return L"";

    wchar_t quote = xml[attrPos];

    if (quote != L'\'' && quote != L'\"')
        return L"";

    size_t valueStart = attrPos + 1;
    size_t valueEnd = xml.find(quote, valueStart);

    if (valueEnd == std::wstring::npos)
        return L"";

    return xml.substr(valueStart, valueEnd - valueStart);
}

static std::wstring ExtractFirstXmlDataText(const std::wstring& xml)
{
    size_t dataPos = xml.find(L"<Data");

    if (dataPos == std::wstring::npos)
        return L"";

    size_t start = xml.find(L'>', dataPos);
    size_t end = xml.find(L"</Data>", start == std::wstring::npos ? dataPos : start);

    if (start == std::wstring::npos || end == std::wstring::npos || end <= start + 1)
        return L"";

    return TrimXmlText(xml.substr(start + 1, end - start - 1));
}

static std::wstring ExtractAllXmlDataText(const std::wstring& xml)
{
    std::wstring out;
    size_t pos = 0;

    while (true)
    {
        size_t dataPos = xml.find(L"<Data", pos);

        if (dataPos == std::wstring::npos)
            break;

        size_t start = xml.find(L'>', dataPos);
        size_t end = xml.find(L"</Data>", start == std::wstring::npos ? dataPos : start);

        if (start == std::wstring::npos || end == std::wstring::npos)
            break;

        if (end > start + 1)
        {
            std::wstring value = TrimXmlText(xml.substr(start + 1, end - start - 1));

            if (!value.empty())
            {
                if (!out.empty())
                    out += L"; ";

                out += value;
            }
        }

        pos = end + 7;
    }

    if (out.empty())
        out = ExtractFirstXmlDataText(xml);

    if (out.size() > 260)
        out = out.substr(0, 257) + L"...";

    return out;
}


static std::wstring ExtractXmlDataTextByName(const std::wstring& xml, const std::wstring& name)
{
    size_t pos = 0;

    while (true)
    {
        size_t dataPos = xml.find(L"<Data", pos);
        if (dataPos == std::wstring::npos)
            break;

        size_t tagEnd = xml.find(L'>', dataPos);
        if (tagEnd == std::wstring::npos)
            break;

        std::wstring nameAttr = ExtractXmlAttribute(xml.substr(dataPos, tagEnd - dataPos + 1), L"Data", L"Name");
        if (_wcsicmp(nameAttr.c_str(), name.c_str()) == 0)
        {
            size_t end = xml.find(L"</Data>", tagEnd);
            if (end == std::wstring::npos)
                return L"";
            return TrimXmlText(xml.substr(tagEnd + 1, end - tagEnd - 1));
        }

        pos = tagEnd + 1;
    }

    return L"";
}

static std::vector<std::wstring> ExtractXmlDataValues(const std::wstring& xml)
{
    std::vector<std::wstring> values;
    size_t pos = 0;

    while (true)
    {
        size_t dataPos = xml.find(L"<Data", pos);
        if (dataPos == std::wstring::npos)
            break;

        size_t start = xml.find(L'>', dataPos);
        size_t end = xml.find(L"</Data>", start == std::wstring::npos ? dataPos : start);
        if (start == std::wstring::npos || end == std::wstring::npos)
            break;

        std::wstring value = TrimXmlText(xml.substr(start + 1, end - start - 1));
        if (!value.empty())
            values.push_back(value);

        pos = end + 7;
    }

    return values;
}

static std::wstring CleanEventDetailText(std::wstring detail);

static std::wstring DecodeXmlEntities(std::wstring value)
{
    struct EntityPair
    {
        const wchar_t* from;
        const wchar_t* to;
    };

    const EntityPair entities[] =
    {
        { L"&amp;", L"&" },
        { L"&lt;", L"<" },
        { L"&gt;", L">" },
        { L"&quot;", L"\"" },
        { L"&apos;", L"'" }
    };

    for (const EntityPair& entity : entities)
    {
        size_t pos = 0;
        while ((pos = value.find(entity.from, pos)) != std::wstring::npos)
        {
            value.replace(pos, wcslen(entity.from), entity.to);
            pos += wcslen(entity.to);
        }
    }

    return value;
}

static std::vector<std::pair<std::wstring, std::wstring>> ExtractXmlDataPairs(const std::wstring& xml)
{
    std::vector<std::pair<std::wstring, std::wstring>> values;
    size_t pos = 0;
    size_t index = 1;

    while (true)
    {
        size_t dataPos = xml.find(L"<Data", pos);
        if (dataPos == std::wstring::npos)
            break;

        size_t tagEnd = xml.find(L'>', dataPos);
        size_t end = xml.find(L"</Data>", tagEnd == std::wstring::npos ? dataPos : tagEnd);
        if (tagEnd == std::wstring::npos || end == std::wstring::npos)
            break;

        std::wstring tag = xml.substr(dataPos, tagEnd - dataPos + 1);
        std::wstring name = ExtractXmlAttribute(tag, L"Data", L"Name");
        std::wstring value = DecodeXmlEntities(TrimXmlText(xml.substr(tagEnd + 1, end - tagEnd - 1)));

        if (!value.empty())
        {
            if (name.empty())
                name = L"Data" + std::to_wstring(index);
            values.emplace_back(name, value);
        }

        ++index;
        pos = end + 7;
    }

    return values;
}

static std::wstring FindNamedEventValue(const std::vector<std::pair<std::wstring, std::wstring>>& values, std::initializer_list<const wchar_t*> names)
{
    for (const wchar_t* wanted : names)
    {
        for (const auto& item : values)
        {
            if (_wcsicmp(item.first.c_str(), wanted) == 0 && !item.second.empty())
                return item.second;
        }
    }

    return L"";
}

static bool LooksLikeExecutableName(const std::wstring& value)
{
    std::wstring lower = ToLowerCopy(value);
    size_t pos = lower.find(L".exe");
    if (pos == std::wstring::npos)
        return false;

    if (pos + 4 < lower.size())
    {
        wchar_t ch = lower[pos + 4];
        if (iswalnum(ch) || ch == L'_' || ch == L'-')
            return false;
    }

    return true;
}

static std::wstring ExtractEventProcessName(const std::wstring& xml)
{
    std::vector<std::pair<std::wstring, std::wstring>> values = ExtractXmlDataPairs(xml);
    std::wstring process = FindNamedEventValue(values,
    {
        L"ProcessName",
        L"Process",
        L"Image",
        L"ImageName",
        L"Application",
        L"Executable",
        L"CallerProcessName",
        L"ClientProcessName"
    });

    if (!process.empty())
        return ExtractFileNameOnly(process);

    for (const auto& item : values)
    {
        if (LooksLikeExecutableName(item.second))
            return ExtractFileNameOnly(item.second);
    }

    return L"";
}

static std::wstring ExtractEventTargetName(const std::wstring& xml)
{
    std::vector<std::pair<std::wstring, std::wstring>> values = ExtractXmlDataPairs(xml);
    std::wstring target = FindNamedEventValue(values,
    {
        L"LogFile",
        L"LogName",
        L"Channel",
        L"TargetLog",
        L"ObjectName",
        L"FileName",
        L"TargetFilename",
        L"Path",
        L"VolumeName",
        L"Volume",
        L"DeviceName",
        L"DeviceInstanceId"
    });

    if (!target.empty())
        return target;

    for (const auto& item : values)
    {
        std::wstring lower = ToLowerCopy(item.second);
        if (lower.find(L"\\") != std::wstring::npos ||
            lower.find(L"volume") != std::wstring::npos ||
            lower.find(L"journal") != std::wstring::npos ||
            lower.find(L"log") != std::wstring::npos)
            return item.second;
    }

    return L"";
}

static std::wstring BuildNamedEventDataSummary(const std::wstring& xml, size_t limit)
{
    std::vector<std::pair<std::wstring, std::wstring>> values = ExtractXmlDataPairs(xml);
    std::wstring out;

    for (const auto& item : values)
    {
        std::wstring name = item.first;
        std::wstring lowerName = ToLowerCopy(name);
        if (lowerName == L"data1" || lowerName == L"data2" || lowerName == L"data3" || lowerName == L"data4")
            name = L"данные";

        std::wstring piece = name + L": " + item.second;
        if (!out.empty())
            piece = L"; " + piece;

        if (out.size() + piece.size() > limit)
            break;

        out += piece;
    }

    return CleanEventDetailText(out);
}

static std::wstring FormatEventMessageFromMetadata(EVT_HANDLE eventHandle, const std::wstring& provider)
{
    if (provider.empty())
        return L"";

    EVT_HANDLE metadata = EvtOpenPublisherMetadata(nullptr, provider.c_str(), nullptr, 0, 0);
    if (!metadata)
        return L"";

    DWORD bufferUsed = 0;
    std::wstring result;

    if (!EvtFormatMessage(metadata, eventHandle, 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &bufferUsed) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<wchar_t> buffer(bufferUsed + 2);
        if (EvtFormatMessage(metadata, eventHandle, 0, 0, nullptr, EvtFormatMessageEvent, bufferUsed, buffer.data(), &bufferUsed))
            result = CleanEventDetailText(buffer.data());
    }

    EvtClose(metadata);
    return result;
}

static bool IsoUtcTextToLocalIsoText(const std::wstring& iso, std::wstring& out)
{
    if (iso.size() < 19)
        return false;

    SYSTEMTIME st{};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

    if (swscanf_s(iso.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return false;

    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = static_cast<WORD>(second);

    FILETIME ft{};
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!SystemTimeToFileTime(&st, &ft) || !FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return false;

    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    out = buffer;
    return true;
}

static long long IsoUtcTextToSortKey(const std::wstring& iso)
{
    if (iso.size() < 19)
        return 0;

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (swscanf_s(iso.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return 0;

    return static_cast<long long>(year) * 10000000000LL +
        static_cast<long long>(month) * 100000000LL +
        static_cast<long long>(day) * 1000000LL +
        static_cast<long long>(hour) * 10000LL +
        static_cast<long long>(minute) * 100LL +
        static_cast<long long>(second);
}

static std::wstring ExtractEvent104ClearedLogName(const std::wstring& xml)
{
    const wchar_t* namedFields[] =
    {
        L"LogFileCleared",
        L"LogFile",
        L"LogName",
        L"Channel",
        L"TargetLog",
        L"Name"
    };

    for (const wchar_t* field : namedFields)
    {
        std::wstring value = ExtractXmlDataTextByName(xml, field);
        if (!value.empty())
            return value;
    }

    std::vector<std::wstring> values = ExtractXmlDataValues(xml);
    for (const std::wstring& value : values)
    {
        std::wstring lower = ToLowerCopy(value);
        if (lower.find(L"system") != std::wstring::npos ||
            lower.find(L"application") != std::wstring::npos ||
            lower.find(L"powershell") != std::wstring::npos ||
            lower.find(L"operational") != std::wstring::npos ||
            lower.find(L"windows") != std::wstring::npos)
            return value;
    }

    if (!values.empty())
        return values.back();

    return L"журнала";
}

static std::wstring CleanEventDetailText(std::wstring detail)
{
    for (wchar_t& ch : detail)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            ch = L' ';
    }

    while (detail.find(L"  ") != std::wstring::npos)
        detail.replace(detail.find(L"  "), 2, L" ");

    detail = TrimXmlText(detail);
    if (detail.size() > 260)
        detail = detail.substr(0, 257) + L"...";
    return detail;
}

static std::wstring BuildCompactEventMessage(DWORD eventId, const std::wstring& xml, const std::wstring& provider, const std::wstring& formatted)
{
    std::wstring message = CleanEventDetailText(DecodeXmlEntities(formatted));

    if (message.empty() && eventId == 104)
    {
        std::wstring logName = ExtractEvent104ClearedLogName(xml);
        if (logName.empty())
            logName = L"журнала";
        message = L"Файл журнала " + logName + L" очищен.";
    }

    if (message.empty())
        message = CleanEventDetailText(ExtractAllXmlDataText(xml));

    if (message.empty())
        message = BuildNamedEventDataSummary(xml, 360);

    if (message.empty() && !provider.empty())
        message = provider;

    std::wstring process = ExtractEventProcessName(xml);
    if (!process.empty() && ToLowerCopy(message).find(ToLowerCopy(process)) == std::wstring::npos)
        message = L"процесс: " + process + L" | " + message;

    std::wstring target = ExtractEventTargetName(xml);
    if (!target.empty() && ToLowerCopy(message).find(ToLowerCopy(target)) == std::wstring::npos)
        message += L" | объект: " + target;

    std::wstring dataSummary = BuildNamedEventDataSummary(xml, 420);
    if (!dataSummary.empty() && ToLowerCopy(message).find(ToLowerCopy(dataSummary.substr(0, std::min<size_t>(dataSummary.size(), 32)))) == std::wstring::npos)
        message += L" | данные: " + dataSummary;

    message = CleanEventDetailText(message);
    if (message.size() > 520)
        message = message.substr(0, 517) + L"...";

    return message.empty() ? L"событие найдено" : message;
}

static std::wstring RenderEventXml(EVT_HANDLE eventHandle)
{
    DWORD bufferUsed = 0;
    DWORD propertyCount = 0;

    if (!EvtRender(nullptr, eventHandle, EvtRenderEventXml, 0, nullptr, &bufferUsed, &propertyCount) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return L"";

    std::vector<wchar_t> buffer((bufferUsed / sizeof(wchar_t)) + 2);

    if (!EvtRender(nullptr, eventHandle, EvtRenderEventXml, bufferUsed, buffer.data(), &bufferUsed, &propertyCount))
        return L"";

    return buffer.data();
}

static void LimitAutomationLines(std::vector<std::wstring>& lines)
{
    if (lines.size() <= AUTOMATION_MAX_LINES)
        return;

    size_t removed = lines.size() - AUTOMATION_MAX_LINES;
    lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(removed));
    lines.insert(lines.begin(), L"показаны последние " + std::to_wstring(AUTOMATION_MAX_LINES) + L" строк, скрыто: " + std::to_wstring(removed));
}

static size_t GetAutomationVisibleLineCapacity(int target)
{
    HWND hwnd = g_hMain;

    if (!hwnd)
        return AUTOMATION_VISIBLE_LINES;

    RECT rc{};
    GetClientRect(hwnd, &rc);

    RECT panel{};
    if (target == AUTOMATION_OUTPUT_EVENTVWR)
        panel = GetAutomationEventPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_HISTORYPW)
        panel = GetAutomationHistoryPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        panel = GetJarActivityPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_JAR_MODIFIED)
        panel = GetJarModifiedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_EXE_DELETED)
        panel = GetExeDeletedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_PF_DELETED)
        panel = GetPfDeletedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_MC_LOGS)
        panel = GetMcLogsPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_PAGE4)
        panel = (g_currentPage == 1) ? GetAutomationBamPanelRect(rc) : GetPage4PanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED)
        panel = GetDllUnsignedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_INJGEN)
        panel = GetPage5InjGenPanelRect(rc);
    else
        panel = GetAutomationBamPanelRect(rc);

    int h = panel.bottom - panel.top;
    int capacity = std::max(1, (h - 34) / 15);
    return static_cast<size_t>(capacity);
}

static int GetMaxAutomationScrollOffsetLocked(int target, const std::vector<std::wstring>& lines)
{
    size_t visible = GetAutomationVisibleLineCapacity(target);

    if (lines.size() <= visible)
        return 0;

    return static_cast<int>(lines.size() - visible);
}

static int& AutomationScrollRef(int target)
{
    if (target == AUTOMATION_OUTPUT_EVENTVWR)
        return g_eventVwrScrollOffset;
    if (target == AUTOMATION_OUTPUT_HISTORYPW)
        return g_historyPwScrollOffset;
    if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        return g_jarActivityScrollOffset;
    if (target == AUTOMATION_OUTPUT_JAR_MODIFIED)
        return g_jarModifiedScrollOffset;
    if (target == AUTOMATION_OUTPUT_EXE_DELETED)
        return g_exeDeletedScrollOffset;
    if (target == AUTOMATION_OUTPUT_PF_DELETED)
        return g_pfDeletedScrollOffset;
    if (target == AUTOMATION_OUTPUT_MC_LOGS)
        return g_mcLogsScrollOffset;
    if (target == AUTOMATION_OUTPUT_PAGE4)
        return g_page4ScrollOffset;
    if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED)
        return g_dllUnsignedScrollOffset;
    if (target == AUTOMATION_OUTPUT_INJGEN)
        return g_injGenScrollOffset;
    return g_bamScrollOffset;
}

static std::vector<std::wstring>& AutomationLinesRef(int target)
{
    if (target == AUTOMATION_OUTPUT_EVENTVWR)
        return g_eventVwrLines;
    if (target == AUTOMATION_OUTPUT_HISTORYPW)
        return g_historyPwLines;
    if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        return g_jarActivityLines;
    if (target == AUTOMATION_OUTPUT_JAR_MODIFIED)
        return g_jarModifiedLines;
    if (target == AUTOMATION_OUTPUT_EXE_DELETED)
        return g_exeDeletedLines;
    if (target == AUTOMATION_OUTPUT_PF_DELETED)
        return g_pfDeletedLines;
    if (target == AUTOMATION_OUTPUT_MC_LOGS)
        return g_mcLogsLines;
    if (target == AUTOMATION_OUTPUT_PAGE4)
        return g_page4Lines;
    if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED)
        return g_dllUnsignedLines;
    if (target == AUTOMATION_OUTPUT_INJGEN)
        return g_injGenLines;
    return g_bamLines;
}

static void ClampAutomationScrollOffsetLocked(int target)
{
    std::vector<std::wstring>& lines = AutomationLinesRef(target);
    int& scroll = AutomationScrollRef(target);
    int maxOffset = GetMaxAutomationScrollOffsetLocked(target, lines);

    if (scroll < 0)
        scroll = 0;

    if (scroll > maxOffset)
        scroll = maxOffset;
}

static void SetAutomationLines(int target, std::vector<std::wstring> lines)
{
    LimitAutomationLines(lines);

    std::lock_guard<std::mutex> lock(g_automationMutex);
    AutomationLinesRef(target) = std::move(lines);
    AutomationScrollRef(target) = 0;
}

static void SetAutomationStatusLine(int target, const std::wstring& status)
{
    std::lock_guard<std::mutex> lock(g_automationMutex);
    AutomationLinesRef(target).assign(1, status);
    AutomationScrollRef(target) = 0;
}

static void GetAutomationSnapshot(int target, std::vector<std::wstring>& lines, int& scrollOffset)
{
    std::lock_guard<std::mutex> lock(g_automationMutex);
    ClampAutomationScrollOffsetLocked(target);
    lines = AutomationLinesRef(target);
    scrollOffset = AutomationScrollRef(target);
}


static std::wstring JoinAutomationLinesForEdit(const std::vector<std::wstring>& lines)
{
    std::wstring text;
    for (const std::wstring& line : lines)
    {
        text += line;
        text += L"\r\n";
    }
    return text;
}

static void SyncAutomationEditScrollToState(int target);

static void UpdateHistoryEditWindow();

static HWND AutomationEditForTarget(int target)
{
    if (target == AUTOMATION_OUTPUT_EVENTVWR) return g_hEventEdit;
    if (target == AUTOMATION_OUTPUT_HISTORYPW) return g_hHistoryEdit;
    if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY) return g_hJarEdit;
    if (target == AUTOMATION_OUTPUT_JAR_MODIFIED) return nullptr;
    if (target == AUTOMATION_OUTPUT_EXE_DELETED) return g_hExeEdit;
    if (target == AUTOMATION_OUTPUT_PF_DELETED) return g_hPfEdit;
    if (target == AUTOMATION_OUTPUT_MC_LOGS) return g_hMcEdit;
    return nullptr;
}


static void SyncAutomationEditScrollToState(int target)
{
    HWND edit = AutomationEditForTarget(target);
    if (!edit)
        return;

    int wanted = 0;
    {
        std::lock_guard<std::mutex> lock(g_automationMutex);
        wanted = AutomationScrollRef(target);
    }

    int current = static_cast<int>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
    int diff = wanted - current;
    if (diff != 0)
        SendMessageW(edit, EM_LINESCROLL, 0, diff);
}

static void UpdateAutomationEditWindow(int target)
{
    HWND edit = AutomationEditForTarget(target);
    if (!edit || g_destroying.load())
        return;
    std::vector<std::wstring> lines;
    {
        std::lock_guard<std::mutex> lock(g_automationMutex);
        lines = AutomationLinesRef(target);
    }
    std::wstring text = JoinAutomationLinesForEdit(lines);
    SetWindowTextW(edit, text.c_str());
    if (target == AUTOMATION_OUTPUT_EVENTVWR || target == AUTOMATION_OUTPUT_HISTORYPW)
        ShowScrollBar(edit, SB_BOTH, FALSE);
    SyncAutomationEditScrollToState(target);
}

static void UpdateHistoryEditWindow()
{
    UpdateAutomationEditWindow(AUTOMATION_OUTPUT_HISTORYPW);
    UpdateAutomationEditWindow(AUTOMATION_OUTPUT_EVENTVWR);
}

static HWND AutomationEditForTarget(int target);
static void SyncAutomationEditScrollToState(int target);

static bool ScrollAutomationOutput(int target, int wheelDelta)
{
    std::lock_guard<std::mutex> lock(g_automationMutex);

    std::vector<std::wstring>& lines = AutomationLinesRef(target);
    int maxOffset = GetMaxAutomationScrollOffsetLocked(target, lines);

    if (maxOffset <= 0)
        return false;

    int& scroll = AutomationScrollRef(target);
    int oldOffset = scroll;
    int step = std::max(1, std::abs(wheelDelta) / WHEEL_DELTA) * 3;

    if (wheelDelta > 0)
        scroll -= step;
    else if (wheelDelta < 0)
        scroll += step;

    ClampAutomationScrollOffsetLocked(target);
    bool changed = scroll != oldOffset;
    if (changed)
    {
    }
    return changed;
}

static RECT GetAutomationPanelRectForTarget(int target, const RECT& rc)
{
    if (target == AUTOMATION_OUTPUT_EVENTVWR) return GetAutomationEventPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_HISTORYPW) return GetAutomationHistoryPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY) return GetJarActivityPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_JAR_MODIFIED) return GetJarModifiedPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_EXE_DELETED) return GetExeDeletedPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_PF_DELETED) return GetPfDeletedPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_MC_LOGS) return GetMcLogsPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_PAGE4) return (g_currentPage == 1) ? GetAutomationBamPanelRect(rc) : GetPage4PanelRect(rc);
    if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED) return GetDllUnsignedPanelRect(rc);
    if (target == AUTOMATION_OUTPUT_INJGEN) return GetPage5InjGenPanelRect(rc);
    return GetAutomationBamPanelRect(rc);
}

static bool GetAutomationScrollGeometryLocked(int target, const RECT& panel, float& trackX, float& trackY, float& trackH, float& thumbH, int& maxOffset)
{
    std::vector<std::wstring>& lines = AutomationLinesRef(target);
    size_t visible = GetAutomationVisibleLineCapacity(target);
    if (lines.size() <= visible)
        return false;

    maxOffset = std::max<int>(1, static_cast<int>(lines.size()) - static_cast<int>(visible));
    float panelW = static_cast<float>(panel.right - panel.left);
    float panelH = static_cast<float>(panel.bottom - panel.top);
    trackX = static_cast<float>(panel.left) + panelW - 10.0f;
    trackY = static_cast<float>(panel.top) + 28.0f;
    trackH = panelH - 40.0f;
    if (trackH < 24.0f)
        return false;
    thumbH = std::max(22.0f, trackH * (static_cast<float>(visible) / static_cast<float>(lines.size())));
    if (thumbH > trackH)
        thumbH = trackH;
    return true;
}

static bool HitAutomationScrollTrack(int target, POINT pt)
{
    if (!g_hMain)
        return false;
    RECT rc{};
    GetClientRect(g_hMain, &rc);
    RECT panel = GetAutomationPanelRectForTarget(target, rc);

    std::lock_guard<std::mutex> lock(g_automationMutex);
    float trackX = 0.0f, trackY = 0.0f, trackH = 0.0f, thumbH = 0.0f;
    int maxOffset = 0;
    if (!GetAutomationScrollGeometryLocked(target, panel, trackX, trackY, trackH, thumbH, maxOffset))
        return false;

    return pt.x >= static_cast<int>(trackX - 18.0f) && pt.x <= static_cast<int>(trackX + 18.0f) &&
        pt.y >= static_cast<int>(trackY) && pt.y <= static_cast<int>(trackY + trackH);
}

static bool SetAutomationScrollFromPoint(int target, POINT pt)
{
    if (!g_hMain)
        return false;

    RECT rc{};
    GetClientRect(g_hMain, &rc);
    RECT panel = GetAutomationPanelRectForTarget(target, rc);

    std::lock_guard<std::mutex> lock(g_automationMutex);
    float trackX = 0.0f, trackY = 0.0f, trackH = 0.0f, thumbH = 0.0f;
    int maxOffset = 0;
    if (!GetAutomationScrollGeometryLocked(target, panel, trackX, trackY, trackH, thumbH, maxOffset))
        return false;

    int& scroll = AutomationScrollRef(target);
    int oldScroll = scroll;
    float usable = std::max(1.0f, trackH - thumbH);
    float pos = static_cast<float>(pt.y) - trackY - thumbH * 0.5f;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > usable) pos = usable;

    float ratio = pos / usable;
    scroll = static_cast<int>(ratio * static_cast<float>(maxOffset) + 0.5f);
    ClampAutomationScrollOffsetLocked(target);
    return scroll != oldScroll;
}

static int AutomationScrollTargetAtPoint(POINT pt)
{
    if (!g_hMain)
        return 0;

    RECT rc{};
    GetClientRect(g_hMain, &rc);

    int targetsPage1[] = { AUTOMATION_OUTPUT_PAGE4, AUTOMATION_OUTPUT_EVENTVWR, AUTOMATION_OUTPUT_HISTORYPW };
    int targetsPage2[] = { AUTOMATION_OUTPUT_JAR_ACTIVITY, AUTOMATION_OUTPUT_EXE_DELETED, AUTOMATION_OUTPUT_DLL_UNSIGNED };
    int targetsPage3[] = { AUTOMATION_OUTPUT_PAGE4 };
    int targetsPage4[] = { AUTOMATION_OUTPUT_INJGEN };

    const int* targets = nullptr;
    int count = 0;
    if (g_currentPage == 1) { targets = targetsPage1; count = 3; }
    else if (g_currentPage == 2) { targets = targetsPage2; count = 3; }
    else if (g_currentPage == 3) { targets = targetsPage3; count = 1; }
    else if (g_currentPage == 4) { targets = targetsPage4; count = 1; }

    for (int i = 0; i < count; ++i)
    {
        RECT panel = GetAutomationPanelRectForTarget(targets[i], rc);
        if (PointInRect(panel, pt) && HitAutomationScrollTrack(targets[i], pt))
            return targets[i];
    }

    return 0;
}

static void InvalidateAutomationPanel(int target)
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT panel{};
    if (target == AUTOMATION_OUTPUT_EVENTVWR)
        panel = GetAutomationEventPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_HISTORYPW)
        panel = GetAutomationHistoryPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        panel = GetJarActivityPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_JAR_MODIFIED)
        panel = GetJarModifiedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_EXE_DELETED)
        panel = GetExeDeletedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_PF_DELETED)
        panel = GetPfDeletedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_MC_LOGS)
        panel = GetMcLogsPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_PAGE4)
        panel = (g_currentPage == 1) ? GetAutomationBamPanelRect(rc) : GetPage4PanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED)
        panel = GetDllUnsignedPanelRect(rc);
    else if (target == AUTOMATION_OUTPUT_INJGEN)
        panel = GetPage5InjGenPanelRect(rc);
    else
        panel = GetAutomationBamPanelRect(rc);
    InflateRect(&panel, 10, 10);
    InvalidateRect(hwnd, &panel, FALSE);
}

static void PostAutomationDone(int target, std::vector<std::wstring>&& lines)
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    AutomationResult* result = new (std::nothrow) AutomationResult();

    if (!result)
        return;

    result->target = target;
    result->lines = std::move(lines);

    if (!PostMessageW(hwnd, WM_APP_AUTOMATION_DONE, 0, reinterpret_cast<LPARAM>(result)))
        delete result;
}

static std::atomic_bool& AutomationWorkingRef(int target)
{
    if (target == AUTOMATION_OUTPUT_EVENTVWR)
        return g_automationEventWorking;
    if (target == AUTOMATION_OUTPUT_HISTORYPW)
        return g_automationHistoryWorking;
    if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        return g_automationJarWorking;
    if (target == AUTOMATION_OUTPUT_JAR_MODIFIED)
        return g_automationJarModifiedWorking;
    if (target == AUTOMATION_OUTPUT_EXE_DELETED)
        return g_automationExeWorking;
    if (target == AUTOMATION_OUTPUT_PF_DELETED)
        return g_automationPfWorking;
    if (target == AUTOMATION_OUTPUT_MC_LOGS)
        return g_automationMcLogsWorking;
    if (target == AUTOMATION_OUTPUT_PAGE4)
        return g_automationPage4Working;
    if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED)
        return g_automationDllWorking;
    if (target == AUTOMATION_OUTPUT_INJGEN)
        return g_automationInjGenWorking;
    return g_automationBamWorking;
}

enum class BamSignatureState
{
    Deleted,
    Signed,
    NotSigned
};

struct BamEntry
{
    std::wstring exeName;
    std::wstring path;
    std::wstring when;
    std::wstring sid;
    BamSignatureState signature = BamSignatureState::NotSigned;
    ULONGLONG fileTimeValue = 0;
};

static bool PathExistsAndIsFile(const std::wstring& path)
{
    if (path.empty() || path.rfind(L"\\Device\\", 0) == 0)
        return false;

    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool IsFileAuthenticodeSigned(const std::wstring& path)
{
    if (!PathExistsAndIsFile(path))
        return false;

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_SAFER_FLAG;

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &policyGuid, &trustData);

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policyGuid, &trustData);

    return status == ERROR_SUCCESS;
}

static BamSignatureState GetBamSignatureState(const std::wstring& path)
{
    if (!PathExistsAndIsFile(path))
        return BamSignatureState::Deleted;

    return IsFileAuthenticodeSigned(path) ? BamSignatureState::Signed : BamSignatureState::NotSigned;
}

static std::wstring BamSignatureStateText(BamSignatureState state)
{
    switch (state)
    {
    case BamSignatureState::Signed:
        return L"Signed";
    case BamSignatureState::Deleted:
        return L"Deleted";
    case BamSignatureState::NotSigned:
    default:
        return L"Not signed";
    }
}

static ULONGLONG FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static std::wstring ShortSidTail(const std::wstring& sid)
{
    size_t pos = sid.find_last_of(L'-');
    if (pos == std::wstring::npos || pos + 1 >= sid.size())
        return sid;

    return L"...-" + sid.substr(pos + 1);
}

static void AppendBamRegistryKey(const wchar_t* subPath, bool unsignedOnly, std::vector<BamEntry>& rows, std::vector<std::wstring>& notes)
{
    HKEY root = nullptr;
    LONG openResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath, 0, KEY_READ | KEY_WOW64_64KEY, &root);
    if (openResult != ERROR_SUCCESS)
        openResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath, 0, KEY_READ | KEY_WOW64_32KEY, &root);
    if (openResult != ERROR_SUCCESS)
        openResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath, 0, KEY_READ, &root);

    if (openResult != ERROR_SUCCESS)
    {
        notes.push_back(L"BAM: не удалось открыть HKLM\\" + std::wstring(subPath) + L".");
        return;
    }

    DWORD subKeyCount = 0;
    DWORD maxSubKeyLen = 0;
    if (RegQueryInfoKeyW(root, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
    {
        notes.push_back(L"BAM: не удалось прочитать список SID в HKLM\\" + std::wstring(subPath) + L".");
        RegCloseKey(root);
        return;
    }

    for (DWORD i = 0; i < subKeyCount; ++i)
    {
        std::vector<wchar_t> sid(maxSubKeyLen + 2, L'\0');
        DWORD sidLen = static_cast<DWORD>(sid.size());

        if (RegEnumKeyExW(root, i, sid.data(), &sidLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            continue;

        std::wstring sidText(sid.data(), sidLen);
        HKEY userKey = nullptr;

        if (RegOpenKeyExW(root, sidText.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &userKey) != ERROR_SUCCESS)
            continue;

        DWORD valueCount = 0;
        DWORD maxValueNameLen = 0;
        DWORD maxValueDataLen = 0;
        if (RegQueryInfoKeyW(userKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr) != ERROR_SUCCESS)
        {
            RegCloseKey(userKey);
            continue;
        }

        for (DWORD valueIndex = 0; valueIndex < valueCount; ++valueIndex)
        {
            std::vector<wchar_t> valueName(maxValueNameLen + 2, L'\0');
            std::vector<BYTE> valueData(std::max<DWORD>(maxValueDataLen + 2, sizeof(FILETIME)));
            DWORD valueNameLen = static_cast<DWORD>(valueName.size());
            DWORD valueDataLen = static_cast<DWORD>(valueData.size());
            DWORD type = 0;

            LONG enumResult = RegEnumValueW(userKey, valueIndex, valueName.data(), &valueNameLen, nullptr, &type, valueData.data(), &valueDataLen);

            if (enumResult != ERROR_SUCCESS || valueNameLen == 0 || type != REG_BINARY)
                continue;

            std::wstring rawPath(valueName.data(), valueNameLen);
            std::wstring displayPath = DevicePathToDosPath(rawPath);

            FILETIME ft{};
            std::wstring when = L"дата неизвестна";
            ULONGLONG ftValue = 0;

            if (TryReadBamFileTime(valueData.data(), valueDataLen, ft))
            {
                when = FileTimeToLocalText(ft);
                ftValue = FileTimeToUInt64(ft);
            }

            BamSignatureState signature = GetBamSignatureState(displayPath);

            // unsignedOnly означает строго только unsigned/not signed.
            // Deleted/отсутствующие файлы и signed в BAM не выводим.
            if (unsignedOnly && signature != BamSignatureState::NotSigned)
                continue;

            BamEntry entry;
            entry.exeName = ExtractFileNameOnly(displayPath);
            entry.path = displayPath;
            entry.when = when;
            entry.sid = ShortSidTail(sidText);
            entry.signature = signature;
            entry.fileTimeValue = ftValue;
            rows.push_back(std::move(entry));
        }

        RegCloseKey(userKey);
    }

    RegCloseKey(root);
}

static std::vector<std::wstring> ScanBamRegistry(bool unsignedOnly)
{
    // BAM в интерфейсе всегда выводит только unsigned/not signed файлы.
    unsignedOnly = true;

    std::vector<std::wstring> lines;
    std::vector<std::wstring> notes;
    std::vector<BamEntry> rows;

    const wchar_t* subPaths[] =
    {
        L"SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings",
        L"SYSTEM\\CurrentControlSet\\Services\\bam\\UserSettings",
        L"SYSTEM\\ControlSet001\\Services\\bam\\State\\UserSettings",
        L"SYSTEM\\ControlSet001\\Services\\bam\\UserSettings",
        L"SYSTEM\\ControlSet002\\Services\\bam\\State\\UserSettings",
        L"SYSTEM\\ControlSet002\\Services\\bam\\UserSettings"
    };

    for (const wchar_t* subPath : subPaths)
        AppendBamRegistryKey(subPath, unsignedOnly, rows, notes);

    std::sort(rows.begin(), rows.end(), [](const BamEntry& a, const BamEntry& b)
    {
        if (a.fileTimeValue == 0 && b.fileTimeValue != 0)
            return false;

        if (a.fileTimeValue != 0 && b.fileTimeValue == 0)
            return true;

        if (a.fileTimeValue != b.fileTimeValue)
            return a.fileTimeValue < b.fileTimeValue;

        return ToLowerCopy(a.path) < ToLowerCopy(b.path);
    });

    rows.erase(std::unique(rows.begin(), rows.end(), [](const BamEntry& a, const BamEntry& b)
    {
        return ToLowerCopy(a.path) == ToLowerCopy(b.path) && a.fileTimeValue == b.fileTimeValue;
    }), rows.end());

    lines.push_back(L"дата | программа | путь | подпись");

    if (rows.empty())
    {
        if (unsignedOnly)
            lines.push_back(L"unsigned записей не найдено или все записи похожи на системные.");
        else
            lines.push_back(L"пользовательских запусков не найдено или все найденные записи похожи на системные.");

        return lines;
    }

    for (const BamEntry& entry : rows)
    {
        lines.push_back(entry.when + L" | " + entry.exeName + L" | " + entry.path + L" | " + BamSignatureStateText(entry.signature));
    }

    return lines;
}


static bool IsSearchIndexerEventText(const std::wstring& text)
{
    std::wstring lower = ToLowerCopy(text);
    return lower.find(L"searchindexer") != std::wstring::npos ||
        lower.find(L"searchindexer.exe") != std::wstring::npos;
}

struct CompactEventRow
{
    DWORD eventId = 0;
    std::wstring timeText;
    std::wstring message;
    long long sortKey = 0;
};

static void AppendEventLogMatches(std::vector<CompactEventRow>& rows, const wchar_t* channel, DWORD eventId)
{
    std::wstring query = L"*[System[(EventID=" + std::to_wstring(eventId) + L")]]";
    EVT_HANDLE queryHandle = EvtQuery(nullptr, channel, query.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection);

    if (!queryHandle)
        return;

    EVT_HANDLE events[16]{};
    DWORD returned = 0;

    while (EvtNext(queryHandle, static_cast<DWORD>((sizeof(events) / sizeof(events[0]))), events, 0, 0, &returned))
    {
        for (DWORD i = 0; i < returned; ++i)
        {
            std::wstring xml = RenderEventXml(events[i]);
            std::wstring provider = ExtractXmlAttribute(xml, L"Provider", L"Name");
            std::wstring isoTime = ExtractXmlAttribute(xml, L"TimeCreated", L"SystemTime");
            std::wstring localTime;

            if (!IsoUtcTextToLocalIsoText(isoTime, localTime))
                localTime = isoTime.empty() ? L"дата неизвестна" : isoTime;

            std::wstring detail = ExtractAllXmlDataText(xml);

            if (IsSearchIndexerEventText(xml) || IsSearchIndexerEventText(provider) || IsSearchIndexerEventText(detail))
            {
                EvtClose(events[i]);
                events[i] = nullptr;
                continue;
            }

            CompactEventRow row;
            row.eventId = eventId;
            row.timeText = localTime;
            row.message = BuildCompactEventMessage(eventId, xml, provider, FormatEventMessageFromMetadata(events[i], provider));
            row.sortKey = IsoUtcTextToSortKey(isoTime);
            rows.push_back(std::move(row));

            EvtClose(events[i]);
            events[i] = nullptr;
        }
    }

    for (EVT_HANDLE& eventHandle : events)
    {
        if (eventHandle)
        {
            EvtClose(eventHandle);
            eventHandle = nullptr;
        }
    }

    EvtClose(queryHandle);
}

static std::vector<std::wstring> ScanEventViewerLogs()
{
    std::vector<CompactEventRow> rows;
    rows.reserve(128);

    AppendEventLogMatches(rows, L"System", 104);
    AppendEventLogMatches(rows, L"Microsoft-Windows-Ntfs/Operational", 501);
    AppendEventLogMatches(rows, L"Application", 3079);

    std::stable_sort(rows.begin(), rows.end(), [](const CompactEventRow& a, const CompactEventRow& b)
    {
        if (a.sortKey != b.sortKey)
            return a.sortKey > b.sortKey;
        return a.eventId < b.eventId;
    });

    std::vector<std::wstring> lines;
    for (const CompactEventRow& row : rows)
    {
        lines.push_back(L"ID " + std::to_wstring(row.eventId) + L" | " + row.timeText + L" | " + row.message);
    }

    if (lines.empty())
        lines.push_back(L"не найдено");

    return lines;
}

static std::wstring DecodeAutomationTextBytes(const std::vector<BYTE>& bytes)
{
    if (bytes.empty())
        return L"";

    const BYTE* data = bytes.data();
    int size = static_cast<int>(bytes.size());

    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return std::wstring(reinterpret_cast<const wchar_t*>(data + 2), (size - 2) / 2);

    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF)
    {
        std::wstring out;
        for (int i = 2; i + 1 < size; i += 2)
            out.push_back(static_cast<wchar_t>((data[i] << 8) | data[i + 1]));
        return out;
    }

    UINT cp = CP_UTF8;
    int offset = 0;
    DWORD flags = MB_ERR_INVALID_CHARS;

    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        offset = 3;

    int needed = MultiByteToWideChar(cp, flags, reinterpret_cast<const char*>(data + offset), size - offset, nullptr, 0);

    if (needed <= 0)
    {
        cp = CP_OEMCP;
        flags = 0;
        offset = 0;
        needed = MultiByteToWideChar(cp, flags, reinterpret_cast<const char*>(data), size, nullptr, 0);
    }

    if (needed <= 0)
        return L"";

    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(cp, flags, reinterpret_cast<const char*>(data + offset), size - offset, &out[0], needed);
    return out;
}

static std::vector<std::wstring> SplitAutomationTextLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    std::wstring current;

    for (wchar_t ch : text)
    {
        if (ch == L'\r')
            continue;

        if (ch == L'\n')
        {
            for (wchar_t& c : current)
            {
                if (c == L'\t')
                    c = L' ';
            }
            lines.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty())
    {
        for (wchar_t& c : current)
        {
            if (c == L'\t')
                c = L' ';
        }
        lines.push_back(current);
    }

    return lines;
}

static void AppendWrappedAutomationLine(std::vector<std::wstring>& lines, const std::wstring& line, size_t maxChars = 150)
{
    if (line.size() <= maxChars)
    {
        lines.push_back(line);
        return;
    }

    size_t pos = 0;
    while (pos < line.size())
    {
        size_t take = std::min(maxChars, line.size() - pos);
        size_t breakAt = std::wstring::npos;

        if (take == maxChars)
        {
            size_t end = pos + take;
            for (size_t i = end; i > pos + maxChars / 2; --i)
            {
                wchar_t ch = line[i - 1];
                if (ch == L' ' || ch == L'|' || ch == L',' || ch == L';' || ch == L'\\' || ch == L'/')
                {
                    breakAt = i;
                    break;
                }
            }
        }

        if (breakAt != std::wstring::npos && breakAt > pos)
            take = breakAt - pos;

        std::wstring part = line.substr(pos, take);
        if (pos != 0)
            part = L"  " + part;
        lines.push_back(part);
        pos += take;
    }
}

static void AppendWrappedAutomationLines(std::vector<std::wstring>& out, const std::vector<std::wstring>& src, size_t maxChars = 150)
{
    for (const std::wstring& line : src)
        AppendWrappedAutomationLine(out, line, maxChars);
}

static std::wstring GetPowerShellHistoryPath()
{
    wchar_t roaming[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", roaming, static_cast<DWORD>(sizeof(roaming) / sizeof(roaming[0])));

    std::wstring base;
    if (n > 0 && n < sizeof(roaming) / sizeof(roaming[0]))
        base.assign(roaming, n);
    else
    {
        wchar_t userProfile[MAX_PATH]{};
        n = GetEnvironmentVariableW(L"USERPROFILE", userProfile, static_cast<DWORD>(sizeof(userProfile) / sizeof(userProfile[0])));
        if (n > 0 && n < sizeof(userProfile) / sizeof(userProfile[0]))
            base = std::wstring(userProfile, n) + L"\\AppData\\Roaming";
    }

    if (base.empty())
        return L"";

    return base + L"\\Microsoft\\Windows\\PowerShell\\PSReadLine\\ConsoleHost_history.txt";
}

static std::vector<std::wstring> ScanPowerShellHistory()
{
    std::vector<std::wstring> lines;
    std::wstring path = GetPowerShellHistoryPath();

    if (path.empty())
    {
        lines.push_back(L"historypw: не удалось получить путь APPDATA.");
        return lines;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        lines.push_back(L"historypw: файл не найден: " + path);
        return lines;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0)
    {
        CloseHandle(file);
        lines.push_back(L"historypw: файл пустой.");
        return lines;
    }

    const DWORD maxRead = 8 * 1024 * 1024;
    DWORD toRead = static_cast<DWORD>(std::min<LONGLONG>(fileSize.QuadPart, maxRead));

    if (fileSize.QuadPart > maxRead)
    {
        LARGE_INTEGER distance{};
        distance.QuadPart = fileSize.QuadPart - maxRead;
        SetFilePointerEx(file, distance, nullptr, FILE_BEGIN);
        lines.push_back(L"показан конец ConsoleHost_history.txt, файл больше 8 MB");
    }

    std::vector<BYTE> bytes(toRead);
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), toRead, &read, nullptr))
    {
        CloseHandle(file);
        lines.push_back(L"historypw: ошибка чтения файла.");
        return lines;
    }

    CloseHandle(file);
    bytes.resize(read);

    std::wstring text = DecodeAutomationTextBytes(bytes);
    std::vector<std::wstring> rawLines = SplitAutomationTextLines(text);

    if (rawLines.empty())
    {
        lines.push_back(L"historypw: история пустая.");
        return lines;
    }

    lines.insert(lines.end(), rawLines.begin(), rawLines.end());
    return lines;
}

static bool RunHiddenCommandCapture(const std::wstring& command, std::wstring& output, DWORD timeoutMs)
{
    output.clear();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return false;

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"cmd.exe /d /c " + command;

    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);

    if (!ok)
    {
        CloseHandle(readPipe);
        return false;
    }

    std::vector<BYTE> bytes;
    const DWORD chunkSize = 4096;
    BYTE buffer[chunkSize];
    DWORD startTick = GetTickCount();
    bool timedOut = false;

    for (;;)
    {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            DWORD read = 0;
            if (ReadFile(readPipe, buffer, std::min<DWORD>(chunkSize, available), &read, nullptr) && read > 0)
                bytes.insert(bytes.end(), buffer, buffer + read);
        }

        DWORD wait = WaitForSingleObject(pi.hProcess, 30);
        if (wait == WAIT_OBJECT_0)
        {
            for (;;)
            {
                DWORD availableTail = 0;
                if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &availableTail, nullptr) || availableTail == 0)
                    break;

                DWORD read = 0;
                if (!ReadFile(readPipe, buffer, std::min<DWORD>(chunkSize, availableTail), &read, nullptr) || read == 0)
                    break;

                bytes.insert(bytes.end(), buffer, buffer + read);
            }
            break;
        }

        if (GetTickCount() - startTick > timeoutMs)
        {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);

    output = DecodeAutomationTextBytes(bytes);

    if (timedOut)
    {
        if (!output.empty())
            output += L"\n";
        output += L"fsutil: команда остановлена по таймауту.";
    }

    return true;
}



static bool RunHiddenProcessCapture(const std::wstring& exePath, const std::wstring& arguments, std::wstring& output, DWORD timeoutMs)
{
    output.clear();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return false;

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    if (!arguments.empty())
        cmdLine += L" " + arguments;

    LPCWSTR appName = (exePath.find(L"\\") != std::wstring::npos || exePath.find(L":") != std::wstring::npos) ? exePath.c_str() : nullptr;
    BOOL ok = CreateProcessW(appName, &cmdLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);

    if (!ok)
    {
        CloseHandle(readPipe);
        return false;
    }

    std::vector<BYTE> bytes;
    const DWORD chunkSize = 4096;
    BYTE buffer[chunkSize];
    DWORD startTick = GetTickCount();
    bool timedOut = false;

    for (;;)
    {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            DWORD read = 0;
            if (ReadFile(readPipe, buffer, std::min<DWORD>(chunkSize, available), &read, nullptr) && read > 0)
                bytes.insert(bytes.end(), buffer, buffer + read);
        }

        DWORD wait = WaitForSingleObject(pi.hProcess, 30);
        if (wait == WAIT_OBJECT_0)
        {
            for (;;)
            {
                DWORD availableTail = 0;
                if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &availableTail, nullptr) || availableTail == 0)
                    break;

                DWORD read = 0;
                if (!ReadFile(readPipe, buffer, std::min<DWORD>(chunkSize, availableTail), &read, nullptr) || read == 0)
                    break;

                bytes.insert(bytes.end(), buffer, buffer + read);
            }
            break;
        }

        if (GetTickCount() - startTick > timeoutMs)
        {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);

    output = DecodeAutomationTextBytes(bytes);

    if (timedOut)
    {
        if (!output.empty())
            output += L"\n";
        output += L"команда остановлена по таймауту.";
    }

    return true;
}

static bool WriteUtf16LeTextFileLocal(const std::wstring& path, const std::wstring& text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const BYTE bom[2] = { 0xFF, 0xFE };
    DWORD written = 0;
    BOOL ok = WriteFile(file, bom, 2, &written, nullptr);

    if (ok && !text.empty())
    {
        DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        ok = WriteFile(file, text.data(), bytes, &written, nullptr);
    }

    CloseHandle(file);
    return ok != FALSE;
}

static std::wstring MakeAutomationTempPs1Path(const wchar_t* prefix)
{
    wchar_t tempDir[MAX_PATH]{};
    DWORD len = GetTempPathW(ARRAYSIZE(tempDir), tempDir);
    std::wstring dir = (len > 0 && len < ARRAYSIZE(tempDir)) ? std::wstring(tempDir, len) : L"C:\\Windows\\Temp\\";
    if (!dir.empty() && dir.back() != L'\\')
        dir.push_back(L'\\');
    return dir + prefix + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount()) + L".ps1";
}

static bool RunHiddenPowerShellScriptCapture(const std::wstring& script, std::wstring& output, DWORD timeoutMs)
{
    std::wstring ps1 = MakeAutomationTempPs1Path(L"holycheck_scan");
    if (!WriteUtf16LeTextFileLocal(ps1, script))
        return false;

    std::wstring psExe = L"powershell.exe";
    wchar_t winDir[MAX_PATH]{};
    UINT winLen = GetWindowsDirectoryW(winDir, ARRAYSIZE(winDir));
    if (winLen > 0 && winLen < ARRAYSIZE(winDir))
    {
        std::wstring sysnative = std::wstring(winDir) + L"\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring system32 = std::wstring(winDir) + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (PathFileExistsW(sysnative.c_str()))
            psExe = sysnative;
        else if (PathFileExistsW(system32.c_str()))
            psExe = system32;
    }

    std::wstring args = L"-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + ps1 + L"\"";
    bool ok = RunHiddenProcessCapture(psExe, args, output, timeoutMs);
    DeleteFileW(ps1.c_str());
    return ok;
}

static std::wstring TrimAutomationField(std::wstring value)
{
    while (!value.empty() && iswspace(value.front()))
        value.erase(value.begin());

    while (!value.empty() && iswspace(value.back()))
        value.pop_back();

    return value;
}

static std::vector<std::wstring> ParseAutomationCsvLine(const std::wstring& line)
{
    std::vector<std::wstring> fields;
    std::wstring cur;
    bool quoted = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        wchar_t ch = line[i];

        if (ch == L'"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == L'"')
            {
                cur.push_back(L'"');
                ++i;
            }
            else
            {
                quoted = !quoted;
            }
            continue;
        }

        if (ch == L',' && !quoted)
        {
            fields.push_back(TrimAutomationField(cur));
            cur.clear();
            continue;
        }

        cur.push_back(ch);
    }

    fields.push_back(TrimAutomationField(cur));
    return fields;
}

static bool TryExtractFirstHexMask(const std::wstring& text, ULONGLONG& mask)
{
    std::wstring lower = ToLowerCopy(text);
    size_t pos = lower.find(L"0x");

    while (pos != std::wstring::npos)
    {
        size_t end = pos + 2;
        while (end < lower.size() && iswxdigit(lower[end]))
            ++end;

        if (end > pos + 2)
        {
            std::wstring token = lower.substr(pos + 2, end - pos - 2);
            wchar_t* tail = nullptr;
            unsigned long long value = wcstoull(token.c_str(), &tail, 16);
            if (tail && *tail == 0)
            {
                mask = static_cast<ULONGLONG>(value);
                return true;
            }
        }

        pos = lower.find(L"0x", pos + 2);
    }

    return false;
}

static bool LooksLikeAutomationDate(const std::wstring& value)
{
    if (value.size() < 8 || value.size() > 42)
        return false;

    int digits = 0;
    bool hasTime = false;
    bool hasDateSep = false;

    for (wchar_t ch : value)
    {
        if (iswdigit(ch))
            ++digits;
        else if (ch == L':')
            hasTime = true;
        else if (ch == L'.' || ch == L'/' || ch == L'-')
            hasDateSep = true;
    }

    return digits >= 6 && hasTime && hasDateSep;
}


static bool TryParseAutomationDateKeyAnywhere(const std::wstring& text, long long& key)
{
    key = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (!iswdigit(text[i]))
            continue;

        int nums[6] = { 0, 0, 0, 0, 0, 0 };
        int count = 0;
        size_t j = i;
        bool sawDateSep = false;
        bool sawTimeSep = false;

        while (j < text.size() && count < 6)
        {
            while (j < text.size() && !iswdigit(text[j]))
            {
                if (text[j] == L'.' || text[j] == L'/' || text[j] == L'-')
                    sawDateSep = true;
                else if (text[j] == L':')
                    sawTimeSep = true;
                else if (text[j] == L'|' || text[j] == L'[' || text[j] == L']')
                {
                    if (count >= 5)
                        break;
                }
                ++j;
            }

            if (j >= text.size() || !iswdigit(text[j]))
                break;

            int v = 0;
            int digits = 0;
            while (j < text.size() && iswdigit(text[j]) && digits < 6)
            {
                v = v * 10 + (text[j] - L'0');
                ++j;
                ++digits;
            }
            nums[count++] = v;
        }

        if (count >= 5 && sawDateSep && sawTimeSep)
        {
            int day = nums[0];
            int month = nums[1];
            int year = nums[2];

            if (nums[0] > 1900)
            {
                year = nums[0];
                month = nums[1];
                day = nums[2];
            }

            int hour = nums[3];
            int minute = nums[4];
            int second = (count >= 6) ? nums[5] : 0;

            if (year < 100)
                year += 2000;

            if (year >= 2000 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
                hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59)
            {
                key = static_cast<long long>(year) * 10000000000LL +
                      static_cast<long long>(month) * 100000000LL +
                      static_cast<long long>(day) * 1000000LL +
                      static_cast<long long>(hour) * 10000LL +
                      static_cast<long long>(minute) * 100LL +
                      static_cast<long long>(second);
                return true;
            }
        }
    }

    return false;
}

static void SortAutomationOutputLinesByDateDesc(std::vector<std::wstring>& lines, size_t headerLines = 1)
{
    if (lines.size() <= headerLines + 1)
        return;

    struct SortRow
    {
        std::wstring line;
        long long key;
        size_t order;
        bool hasDate;
    };

    std::vector<SortRow> rows;
    rows.reserve(lines.size() - headerLines);

    for (size_t i = headerLines; i < lines.size(); ++i)
    {
        long long key = 0;
        bool hasDate = TryParseAutomationDateKeyAnywhere(lines[i], key);
        rows.push_back({ lines[i], key, i, hasDate });
    }

    std::stable_sort(rows.begin(), rows.end(), [](const SortRow& a, const SortRow& b)
    {
        if (a.hasDate != b.hasDate)
            return a.hasDate;
        if (a.hasDate && b.hasDate && a.key != b.key)
            return a.key > b.key;
        return a.order < b.order;
    });

    for (size_t i = 0; i < rows.size(); ++i)
        lines[headerLines + i] = rows[i].line;
}


static bool TryParseAutomationDateKeyInPipeColumn(const std::wstring& line, size_t column, long long& key)
{
    std::wstring clean = line;
    if (!clean.empty() && clean[0] == L'\xE000')
        clean.erase(clean.begin());
    size_t start = 0;
    for (size_t c = 0; ; ++c)
    {
        size_t pos = clean.find(L" | ", start);
        std::wstring part = (pos == std::wstring::npos) ? clean.substr(start) : clean.substr(start, pos - start);
        if (c == column)
            return TryParseAutomationDateKeyAnywhere(part, key);
        if (pos == std::wstring::npos)
            break;
        start = pos + 3;
    }
    return false;
}

static void SortAutomationOutputLinesByPipeColumnDateDesc(std::vector<std::wstring>& lines, size_t column, size_t headerLines = 1)
{
    if (lines.size() <= headerLines + 1)
        return;

    struct SortRow
    {
        std::wstring line;
        long long key;
        size_t order;
        bool hasDate;
    };

    std::vector<SortRow> rows;
    rows.reserve(lines.size() - headerLines);
    for (size_t i = headerLines; i < lines.size(); ++i)
    {
        long long key = 0;
        bool hasDate = TryParseAutomationDateKeyInPipeColumn(lines[i], column, key);
        rows.push_back({ lines[i], key, i, hasDate });
    }

    std::stable_sort(rows.begin(), rows.end(), [](const SortRow& a, const SortRow& b)
    {
        if (a.hasDate != b.hasDate)
            return a.hasDate;
        if (a.hasDate && b.hasDate && a.key != b.key)
            return a.key > b.key;
        return a.order < b.order;
    });

    for (size_t i = 0; i < rows.size(); ++i)
        lines[headerLines + i] = rows[i].line;
}

static bool SortAutomationTargetLinesByPipeColumnDateDesc(int target, size_t column)
{
    std::lock_guard<std::mutex> lock(g_automationMutex);
    std::vector<std::wstring>& lines = AutomationLinesRef(target);
    if (lines.size() <= 2)
        return false;

    std::vector<std::wstring> before = lines;
    SortAutomationOutputLinesByPipeColumnDateDesc(lines, column, 1);
    AutomationScrollRef(target) = 0;
    return before != lines;
}

static std::wstring PickUsnFileNameFromCsvFields(const std::vector<std::wstring>& fields, const std::wstring& rawLine)
{
    if (fields.size() > 1)
    {
        std::wstring candidate = TrimAutomationField(fields[1]);
        std::wstring lowerCandidate = ToLowerCopy(candidate);
        if (!candidate.empty() && lowerCandidate != L"name" && lowerCandidate != L"file name" && lowerCandidate != L"filename")
            return ExtractFileNameOnly(candidate);
    }

    for (const std::wstring& field : fields)
    {
        std::wstring lower = ToLowerCopy(field);
        if (lower.find(L".jar") != std::wstring::npos || lower.find(L"jar") != std::wstring::npos)
            return ExtractFileNameOnly(field);
    }

    std::wstring lower = ToLowerCopy(rawLine);
    size_t jarPos = lower.find(L"jar");
    if (jarPos == std::wstring::npos)
        return L"";

    size_t start = jarPos;
    while (start > 0)
    {
        wchar_t ch = rawLine[start - 1];
        if (ch == L',' || ch == L'"' || ch == L'|' || ch == L' ' || ch == L'\t')
            break;
        --start;
    }

    size_t end = jarPos + 3;
    while (end < rawLine.size())
    {
        wchar_t ch = rawLine[end];
        if (ch == L',' || ch == L'"' || ch == L'|' || ch == L' ' || ch == L'\t')
            break;
        ++end;
    }

    return ExtractFileNameOnly(rawLine.substr(start, end - start));
}

static bool ContainsExtensionToken(const std::wstring& text, const std::wstring& ext);
static std::wstring CleanPipeCell(std::wstring value);


static std::wstring PickUsnFileNameFromCsvFieldsGeneric(const std::vector<std::wstring>& fields, const std::wstring& rawLine, const std::wstring& ext)
{
    if (fields.size() > 1)
    {
        std::wstring candidate = TrimAutomationField(fields[1]);
        std::wstring lowerCandidate = ToLowerCopy(candidate);
        if (!candidate.empty() && lowerCandidate != L"name" && lowerCandidate != L"file name" && lowerCandidate != L"filename")
            return ExtractFileNameOnly(candidate);
    }

    for (const std::wstring& field : fields)
    {
        if (ContainsExtensionToken(field, ext))
            return ExtractFileNameOnly(field);
    }

    std::wstring lower = ToLowerCopy(rawLine);
    std::wstring lowerExt = ToLowerCopy(ext);
    size_t extPos = lower.find(lowerExt);
    if (extPos == std::wstring::npos)
        return L"";

    size_t start = extPos;
    while (start > 0)
    {
        wchar_t ch = rawLine[start - 1];
        if (ch == L',' || ch == L'"' || ch == L'|' || ch == L' ' || ch == L'\t')
            break;
        --start;
    }

    size_t endPos = extPos + lowerExt.size();
    while (endPos < rawLine.size())
    {
        wchar_t ch = rawLine[endPos];
        if (ch == L',' || ch == L'"' || ch == L'|' || ch == L' ' || ch == L'\t')
            break;
        ++endPos;
    }

    return ExtractFileNameOnly(rawLine.substr(start, endPos - start));
}

static bool ContainsExtensionToken(const std::wstring& text, const std::wstring& ext)
{
    if (text.empty() || ext.empty())
        return false;

    std::wstring name = ToLowerCopy(ExtractFileNameOnly(text));
    std::wstring lowerExt = ToLowerCopy(ext);

    if (name.empty())
        return false;

    if (lowerExt == L".jar")
    {
        size_t pos = name.find(L".jar");
        if (pos == std::wstring::npos)
            return false;

        size_t after = pos + 4;
        return after == name.size() || name.find(L".jar.", pos) != std::wstring::npos;
    }

    if (name.size() < lowerExt.size())
        return false;

    return name.compare(name.size() - lowerExt.size(), lowerExt.size(), lowerExt) == 0;
}

static std::wstring ExtractUsnPathLikeForExtension(const std::wstring& text, const std::wstring& ext)
{
    std::wstring clean = TrimAutomationField(text);
    if (clean.empty())
        return L"";

    std::wstring lower = ToLowerCopy(clean);
    std::wstring lowerExt = ToLowerCopy(ext);
    size_t extPos = lower.find(lowerExt);
    if (extPos == std::wstring::npos)
        return L"";

    size_t endPos = extPos + lowerExt.size();
    while (endPos < clean.size())
    {
        wchar_t ch = clean[endPos];
        if (ch == L'"' || ch == L'|' || ch == L',' || ch == L'\r' || ch == L'\n' || ch == L'\t')
            break;
        ++endPos;
    }

    size_t start = std::wstring::npos;
    size_t drivePos = lower.rfind(L":\\", extPos);
    if (drivePos != std::wstring::npos && drivePos > 0)
        start = drivePos - 1;
    else
    {
        size_t devPos = lower.rfind(L"\\device\\", extPos);
        if (devPos != std::wstring::npos)
            start = devPos;
    }

    if (start == std::wstring::npos)
        return L"";

    return CleanPipeCell(DevicePathToDosPath(clean.substr(start, endPos - start)));
}

static std::wstring PickUsnPathFromCsvFieldsGeneric(const std::vector<std::wstring>& fields, const std::wstring& rawLine, const std::wstring& ext)
{
    // USN usually contains only the file name. If a full path is present in CSV,
    // show it; otherwise keep the filename in the path column instead of leaving it empty.
    for (const std::wstring& field : fields)
    {
        std::wstring path = ExtractUsnPathLikeForExtension(field, ext);
        if (!path.empty())
            return path;
    }

    std::wstring path = ExtractUsnPathLikeForExtension(rawLine, ext);
    if (!path.empty())
        return path;

    return CleanPipeCell(PickUsnFileNameFromCsvFieldsGeneric(fields, rawLine, ext));
}


static std::wstring NormalizeUsnFileReferenceField(std::wstring value)
{
    value = TrimAutomationField(value);
    if (value.empty())
        return L"";

    while (!value.empty() && (value.front() == L'"' || value.front() == L'['))
        value.erase(value.begin());
    while (!value.empty() && (value.back() == L'"' || value.back() == L']' || value.back() == L','))
        value.pop_back();

    if (value.empty())
        return L"";

    std::wstring lower = ToLowerCopy(value);
    bool hasHexPrefix = lower.rfind(L"0x", 0) == 0;
    std::wstring digits = hasHexPrefix ? lower.substr(2) : lower;
    if (digits.empty())
        return L"";

    bool allDecimal = true;
    bool allHex = true;
    bool hasHexLetter = false;
    for (wchar_t ch : digits)
    {
        if (!iswdigit(ch))
            allDecimal = false;
        if (!iswxdigit(ch))
            allHex = false;
        if ((ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F'))
            hasHexLetter = true;
    }

    if (hasHexPrefix)
    {
        if (!allHex || digits.size() > 16)
            return L"";
        return L"0x" + digits;
    }

    // fsutil csv can print file reference numbers as decimal.  Do not convert
    // decimal-looking values to hex: queryfilenamebyid accepts the decimal form,
    // while adding 0x changes the value and breaks parent path recovery.
    if (allDecimal)
        return digits;

    if (allHex && hasHexLetter && digits.size() <= 16)
        return L"0x" + digits;

    return L"";
}

static void PickUsnReferenceFields(const std::vector<std::wstring>& fields, std::wstring& fileRef, std::wstring& parentRef)
{
    fileRef.clear();
    parentRef.clear();

    // fsutil usn readjournal C: csv normally starts with:
    // USN | File name | File reference number | Parent file reference number | Reason | Time stamp | ...
    if (fields.size() > 2)
        fileRef = NormalizeUsnFileReferenceField(fields[2]);
    if (fields.size() > 3)
        parentRef = NormalizeUsnFileReferenceField(fields[3]);
}

static std::wstring ExtractDosPathFromFsutilFileIdOutput(const std::wstring& output)
{
    std::wstring text = output;
    size_t start = text.find(L":\\");
    if (start == std::wstring::npos || start == 0)
        return L"";

    --start; // include the drive letter
    size_t end = text.find_first_of(L"\r\n", start);
    std::wstring path = (end == std::wstring::npos) ? text.substr(start) : text.substr(start, end - start);

    while (!path.empty() && (path.front() == L'"' || path.front() == L'[' || path.front() == L' ' || path.front() == L'\t'))
        path.erase(path.begin());
    while (!path.empty() && (path.back() == L'"' || path.back() == L']' || path.back() == L' ' || path.back() == L'\t' || path.back() == L'.'))
        path.pop_back();

    return CleanPipeCell(DevicePathToDosPath(path));
}

static std::wstring QueryNtfsPathByFileReferenceCached(const std::wstring& fileRef)
{
    static std::mutex cacheMutex;
    static std::map<std::wstring, std::wstring> cache;

    std::wstring ref = NormalizeUsnFileReferenceField(fileRef);
    if (ref.empty())
        return L"";

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(ref);
        if (it != cache.end())
            return it->second;
    }

    std::wstring output;
    std::wstring command = L"fsutil file queryfilenamebyid C: " + ref;
    std::wstring result;
    if (RunHiddenCommandCapture(command, output, 12000))
        result = ExtractDosPathFromFsutilFileIdOutput(output);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[ref] = result;
    }

    return result;
}


static bool ContainsJarExtensionToken(const std::wstring& text)
{
    return ContainsExtensionToken(text, L".jar");
}

static std::wstring PickJarDateFromCsvFields(const std::vector<std::wstring>& fields)
{
    if (fields.size() > 5 && LooksLikeAutomationDate(fields[5]))
        return fields[5];

    for (const std::wstring& field : fields)
    {
        if (LooksLikeAutomationDate(field))
            return field;
    }

    return L"-";
}

static ULONGLONG PickJarReasonMaskFromCsvFields(const std::vector<std::wstring>& fields, const std::wstring& rawLine)
{
    ULONGLONG mask = 0;

    for (const std::wstring& field : fields)
    {
        if (TryExtractFirstHexMask(field, mask))
            return mask;
    }

    if (TryExtractFirstHexMask(rawLine, mask))
        return mask;

    return 0;
}

static std::wstring NormalizeUsnReasonText(std::wstring text)
{
    text = TrimAutomationField(text);
    if (text.empty())
        return L"activity";

    size_t pos = 0;
    while ((pos = text.find(L"  ", pos)) != std::wstring::npos)
    {
        text.replace(pos, 2, L" ");
        if (pos > 0)
            --pos;
    }

    return text;
}

static void AddReasonPart(std::vector<std::wstring>& parts, ULONGLONG mask, ULONGLONG bit, const wchar_t* text)
{
    if ((mask & bit) != 0)
        parts.push_back(text);
}

static std::wstring JoinReasonParts(const std::vector<std::wstring>& parts)
{
    if (parts.empty())
        return L"activity";

    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            out += L" | ";
        out += parts[i];
    }
    return out;
}

static std::wstring ReasonTextFromMask(ULONGLONG mask)
{
    std::vector<std::wstring> parts;
    AddReasonPart(parts, mask, 0x00000001ULL, L"Data overwrite");
    AddReasonPart(parts, mask, 0x00000002ULL, L"Data extend");
    AddReasonPart(parts, mask, 0x00000004ULL, L"Data truncation");
    AddReasonPart(parts, mask, 0x00000100ULL, L"File create");
    AddReasonPart(parts, mask, 0x00000200ULL, L"File delete");
    AddReasonPart(parts, mask, 0x00001000ULL, L"Rename old name");
    AddReasonPart(parts, mask, 0x00002000ULL, L"Rename new name");
    AddReasonPart(parts, mask, 0x00008000ULL, L"Basic info change");
    AddReasonPart(parts, mask, 0x00200000ULL, L"Stream change");
    AddReasonPart(parts, mask, 0x80000000ULL, L"Close");
    return JoinReasonParts(parts);
}

static bool FieldLooksLikeReasonText(const std::wstring& field)
{
    std::wstring lower = ToLowerCopy(field);
    return lower.find(L"data ") != std::wstring::npos ||
        lower.find(L"file ") != std::wstring::npos ||
        lower.find(L"rename") != std::wstring::npos ||
        lower.find(L"basic info") != std::wstring::npos ||
        lower.find(L"stream") != std::wstring::npos ||
        lower.find(L"close") != std::wstring::npos;
}

static std::wstring PickUsnReasonTextFromCsvFields(const std::vector<std::wstring>& fields, const std::wstring& rawLine, ULONGLONG mask)
{
    if (fields.size() > 6 && FieldLooksLikeReasonText(fields[6]))
        return NormalizeUsnReasonText(fields[6]);

    for (const std::wstring& field : fields)
    {
        if (FieldLooksLikeReasonText(field))
            return NormalizeUsnReasonText(field);
    }

    if (mask != 0)
        return ReasonTextFromMask(mask);

    if (FieldLooksLikeReasonText(rawLine))
        return NormalizeUsnReasonText(rawLine);

    return L"activity";
}

struct JarUsnEntry
{
    std::wstring date;
    std::wstring name;
    std::wstring path;
    std::wstring fileRef;
    std::wstring parentRef;
    std::wstring lowerRaw;
    ULONGLONG reason = 0;
    std::wstring reasonText;
    bool hasJarExtension = false;
    bool created = false;
    bool deleted = false;
    bool renameOld = false;
    bool renameNew = false;
    bool dataExtend = false;
    bool basicInfo = false;
    bool streamChange = false;
    bool close = false;
};

static std::wstring ResolveUsnFullPathForEntry(const JarUsnEntry& e, const std::wstring& ext)
{
    std::wstring path = CleanPipeCell(DevicePathToDosPath(e.path));
    if (path.find(L":\\") != std::wstring::npos || ToLowerCopy(path).find(L"\\device\\") != std::wstring::npos)
        return path;

    std::wstring name = CleanPipeCell(e.name.empty() ? ExtractFileNameOnly(path) : e.name);
    if (name.empty())
        name = ExtractFileNameOnly(path);
    if (name.empty())
        return path;

    if (!e.deleted && !e.fileRef.empty())
    {
        std::wstring exact = QueryNtfsPathByFileReferenceCached(e.fileRef);
        if (!exact.empty() && ContainsExtensionToken(exact, ext))
            return exact;
    }

    if (!e.parentRef.empty())
    {
        std::wstring parent = QueryNtfsPathByFileReferenceCached(e.parentRef);
        if (!parent.empty())
        {
            while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/'))
                parent.pop_back();
            if (!parent.empty())
                return CleanPipeCell(parent + L"\\" + ExtractFileNameOnly(name));
        }
    }

    return CleanPipeCell(path.empty() ? name : path);
}


static bool TextHasAny(const std::wstring& lower, const wchar_t* a, const wchar_t* b = L"", const wchar_t* c = L"")
{
    if (a && *a && lower.find(a) != std::wstring::npos)
        return true;
    if (b && *b && lower.find(b) != std::wstring::npos)
        return true;
    if (c && *c && lower.find(c) != std::wstring::npos)
        return true;
    return false;
}

static JarUsnEntry MakeJarUsnEntry(const std::wstring& rawLine)
{
    std::vector<std::wstring> fields = ParseAutomationCsvLine(rawLine);

    JarUsnEntry e;
    e.date = PickJarDateFromCsvFields(fields);
    e.name = PickUsnFileNameFromCsvFields(fields, rawLine);
    e.path = CleanPipeCell(e.name);
    PickUsnReferenceFields(fields, e.fileRef, e.parentRef);
    e.lowerRaw = ToLowerCopy(rawLine);
    e.reason = PickJarReasonMaskFromCsvFields(fields, rawLine);
    e.reasonText = PickUsnReasonTextFromCsvFields(fields, rawLine, e.reason);
    e.hasJarExtension = ContainsJarExtensionToken(e.name) || ContainsJarExtensionToken(rawLine);

    e.created = ((e.reason & 0x00000100ULL) != 0) || TextHasAny(e.lowerRaw, L"file_create", L"file create", L"filecreate");
    e.deleted = ((e.reason & 0x00000200ULL) != 0) || TextHasAny(e.lowerRaw, L"file_delete", L"file delete", L"filedelete");
    e.renameOld = ((e.reason & 0x00001000ULL) != 0) || TextHasAny(e.lowerRaw, L"rename: old name", L"rename old", L"old name");
    e.renameNew = ((e.reason & 0x00002000ULL) != 0) || TextHasAny(e.lowerRaw, L"rename: new name", L"rename new", L"new name");
    e.dataExtend = ((e.reason & 0x00000002ULL) != 0) || TextHasAny(e.lowerRaw, L"data_extend", L"data extend");
    e.basicInfo = ((e.reason & 0x00008000ULL) != 0) || TextHasAny(e.lowerRaw, L"basic_info_change", L"basic info change");
    e.streamChange = ((e.reason & 0x00200000ULL) != 0) || TextHasAny(e.lowerRaw, L"stream_change", L"stream change");
    e.close = ((e.reason & 0x80000000ULL) != 0) || TextHasAny(e.lowerRaw, L"close", L"reason_close", L"usn_reason_close");

    return e;
}

static JarUsnEntry MakeUsnEntryForExtension(const std::wstring& rawLine, const std::wstring& ext)
{
    std::vector<std::wstring> fields = ParseAutomationCsvLine(rawLine);

    JarUsnEntry e;
    e.date = PickJarDateFromCsvFields(fields);
    e.name = PickUsnFileNameFromCsvFieldsGeneric(fields, rawLine, ext);
    e.path = PickUsnPathFromCsvFieldsGeneric(fields, rawLine, ext);
    PickUsnReferenceFields(fields, e.fileRef, e.parentRef);
    e.lowerRaw = ToLowerCopy(rawLine);
    e.reason = PickJarReasonMaskFromCsvFields(fields, rawLine);
    e.reasonText = PickUsnReasonTextFromCsvFields(fields, rawLine, e.reason);
    e.hasJarExtension = ContainsExtensionToken(e.name, ext) || ContainsExtensionToken(rawLine, ext);

    e.created = ((e.reason & 0x00000100ULL) != 0) || TextHasAny(e.lowerRaw, L"file_create", L"file create", L"filecreate");
    e.deleted = ((e.reason & 0x00000200ULL) != 0) || TextHasAny(e.lowerRaw, L"file_delete", L"file delete", L"filedelete");
    e.renameOld = ((e.reason & 0x00001000ULL) != 0) || TextHasAny(e.lowerRaw, L"rename: old name", L"rename old", L"old name");
    e.renameNew = ((e.reason & 0x00002000ULL) != 0) || TextHasAny(e.lowerRaw, L"rename: new name", L"rename new", L"new name");
    e.dataExtend = ((e.reason & 0x00000002ULL) != 0) || TextHasAny(e.lowerRaw, L"data_extend", L"data extend");
    e.basicInfo = ((e.reason & 0x00008000ULL) != 0) || TextHasAny(e.lowerRaw, L"basic_info_change", L"basic info change");
    e.streamChange = ((e.reason & 0x00200000ULL) != 0) || TextHasAny(e.lowerRaw, L"stream_change", L"stream change");
    e.close = ((e.reason & 0x80000000ULL) != 0) || TextHasAny(e.lowerRaw, L"close", L"reason_close", L"usn_reason_close");

    return e;
}

static bool ShouldShowUsnEntryForExtension(const JarUsnEntry& e, const std::wstring& ext)
{
    return e.hasJarExtension && ContainsExtensionToken(e.name, ext);
}

static bool ShouldShowJarUsnEntry(const JarUsnEntry& e)
{
    if (!e.hasJarExtension)
        return false;

    return ContainsJarExtensionToken(e.name);
}

static std::wstring CleanPipeCell(std::wstring value)
{
    for (wchar_t& ch : value)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            ch = L' ';
    }

    size_t pos = 0;
    while ((pos = value.find(L" | ", pos)) != std::wstring::npos)
    {
        value.replace(pos, 3, L", ");
        pos += 2;
    }

    while ((pos = value.find(L"  ")) != std::wstring::npos)
        value.replace(pos, 2, L" ");

    return TrimAutomationField(value);
}

static const wchar_t* AUTOMATION_DELETED_MARKER = L"\xE000";

static bool AutomationLineHasDeletedMarker(const std::wstring& line)
{
    return !line.empty() && line[0] == L'\xE000';
}

static std::wstring StripAutomationDeletedMarker(std::wstring line)
{
    if (AutomationLineHasDeletedMarker(line))
        line.erase(line.begin());
    return line;
}

static std::wstring BuildProgramNameAndPathCell(const std::wstring& pathOrName)
{
    std::wstring clean = CleanPipeCell(DevicePathToDosPath(pathOrName));
    if (clean.empty())
        return L"";

    std::wstring name = ExtractFileNameOnly(clean);
    if (name.empty() || _wcsicmp(name.c_str(), clean.c_str()) == 0)
        return clean;

    return CleanPipeCell(name) + L"    " + clean;
}

static void AppendJarActivityRow(std::vector<std::wstring>& lines, const std::wstring& date, const wchar_t* action, const std::wstring& name, const std::wstring& path = L"")
{
    if (name.empty() && path.empty())
        return;

    std::wstring cleanDate = CleanPipeCell(date);
    std::wstring cleanAction = ToLowerCopy(CleanPipeCell(action ? std::wstring(action) : std::wstring()));
    std::wstring cleanPath = CleanPipeCell(DevicePathToDosPath(path.empty() ? name : path));
    std::wstring program = CleanPipeCell(name.empty() ? ExtractFileNameOnly(cleanPath) : ExtractFileNameOnly(name));
    if (program.empty())
        program = ExtractFileNameOnly(cleanPath);
    if (program.empty())
        program = cleanPath;
    if (program.empty() || cleanPath.empty())
        return;

    std::wstring row = cleanDate + L" | " + program + L" | " + cleanPath;
    if (cleanAction.find(L"deleted") != std::wstring::npos ||
        cleanAction.find(L"file delete") != std::wstring::npos ||
        cleanAction.find(L"file_delete") != std::wstring::npos)
    {
        row.insert(0, AUTOMATION_DELETED_MARKER);
    }

    lines.push_back(std::move(row));
}


static std::wstring SimpleJarJournalReason(const JarUsnEntry& e)
{
    if (e.created) return L"created";
    if (e.deleted) return L"deleted";
    if (e.renameOld) return L"rename old name";
    if (e.renameNew) return L"rename new name";
    return L"";
}


static std::wstring BuildJarActivityReasonWithUsnDate(const std::wstring& reason, const std::wstring& usnDate)
{
    std::wstring cleanReason = CleanPipeCell(reason);
    std::wstring cleanUsnDate = CleanPipeCell(usnDate);
    if (cleanReason.empty())
        return L"";
    if (cleanUsnDate.empty() || cleanUsnDate == L"дата неизвестна")
        return cleanReason;
    return cleanReason + L" (" + cleanUsnDate + L")";
}


static bool IsUnknownAutomationDate(const std::wstring& text);

static std::wstring FileLastWriteTimeText(const std::wstring& path, ULONGLONG* outSortTime = nullptr)
{
    if (outSortTime)
        *outSortTime = 0;

    std::wstring dosPath = DevicePathToDosPath(path);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (dosPath.empty() || GetFileAttributesExW(dosPath.c_str(), GetFileExInfoStandard, &data) == FALSE || !IsPlausibleFileTime(data.ftLastWriteTime))
        return L"дата неизвестна";

    if (outSortTime)
        *outSortTime = FileTimeToUInt64(data.ftLastWriteTime);
    return FileTimeToLocalText(data.ftLastWriteTime);
}

static void AddJarSearchRootIfExists(std::vector<std::wstring>& roots, const std::wstring& path)
{
    if (path.empty())
        return;

    std::wstring clean = DevicePathToDosPath(path);
    while (!clean.empty() && (clean.back() == L'\\' || clean.back() == L'/'))
        clean.pop_back();
    if (clean.empty())
        return;

    DWORD attrs = GetFileAttributesW(clean.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return;

    std::wstring lower = ToLowerCopy(clean);
    for (const std::wstring& existing : roots)
    {
        if (ToLowerCopy(existing) == lower)
            return;
    }
    roots.push_back(clean);
}

static void SearchJarModificationRecursive(const std::wstring& root, const std::wstring& lowerName, int depth, int& visited, ULONGLONG& bestTime, std::wstring& bestText, std::wstring& bestPath)
{
    if (root.empty() || depth < 0 || visited > 14000)
        return;

    std::wstring mask = root + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW(mask.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const wchar_t* name = data.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
            continue;

        std::wstring full = root + L"\\" + name;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            std::wstring lowerDir = ToLowerCopy(name);
            if (lowerDir == L"windows" || lowerDir == L"program files" || lowerDir == L"program files (x86)" ||
                lowerDir == L"programdata" || lowerDir == L"$recycle.bin" || lowerDir == L"system volume information")
                continue;
            SearchJarModificationRecursive(full, lowerName, depth - 1, visited, bestTime, bestText, bestPath);
            continue;
        }

        ++visited;
        if (ToLowerCopy(name) != lowerName)
            continue;

        if (!IsPlausibleFileTime(data.ftLastWriteTime))
            continue;

        ULONGLONG t = FileTimeToUInt64(data.ftLastWriteTime);
        if (t >= bestTime)
        {
            bestTime = t;
            bestText = FileTimeToLocalText(data.ftLastWriteTime);
            bestPath = CleanPipeCell(full);
        }
    } while (FindNextFileW(h, &data));

    FindClose(h);
}

static bool FindJarModificationByName(const std::wstring& fileName, std::wstring& outDate, ULONGLONG* outSortTime = nullptr, std::wstring* outPath = nullptr)
{
    if (outSortTime)
        *outSortTime = 0;
    if (outPath)
        outPath->clear();

    std::wstring name = ExtractFileNameOnly(fileName);
    if (name.empty() || !ContainsJarExtensionToken(name))
        return false;

    static std::mutex cacheMutex;
    static std::map<std::wstring, std::pair<ULONGLONG, std::pair<std::wstring, std::wstring>>> cache;

    std::wstring lowerName = ToLowerCopy(name);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(lowerName);
        if (it != cache.end())
        {
            if (it->second.first == 0)
                return false;
            if (outSortTime)
                *outSortTime = it->second.first;
            outDate = it->second.second.first;
            if (outPath)
                *outPath = it->second.second.second;
            return true;
        }
    }

    std::vector<std::wstring> roots;
    wchar_t userProfile[MAX_PATH]{};
    wchar_t appData[MAX_PATH]{};
    wchar_t localAppData[MAX_PATH]{};
    wchar_t publicProfile[MAX_PATH]{};

    GetEnvironmentVariableW(L"USERPROFILE", userProfile, ARRAYSIZE(userProfile));
    GetEnvironmentVariableW(L"APPDATA", appData, ARRAYSIZE(appData));
    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, ARRAYSIZE(localAppData));
    GetEnvironmentVariableW(L"PUBLIC", publicProfile, ARRAYSIZE(publicProfile));

    if (userProfile[0])
    {
        AddJarSearchRootIfExists(roots, std::wstring(userProfile) + L"\\Desktop");
        AddJarSearchRootIfExists(roots, std::wstring(userProfile) + L"\\Downloads");
        AddJarSearchRootIfExists(roots, std::wstring(userProfile) + L"\\Documents");
        AddJarSearchRootIfExists(roots, std::wstring(userProfile) + L"\\AppData\\Roaming\\.minecraft");
        AddJarSearchRootIfExists(roots, std::wstring(userProfile));
    }
    if (appData[0])
    {
        AddJarSearchRootIfExists(roots, std::wstring(appData) + L"\\.minecraft");
        AddJarSearchRootIfExists(roots, appData);
    }
    if (localAppData[0])
        AddJarSearchRootIfExists(roots, localAppData);
    if (publicProfile[0])
    {
        AddJarSearchRootIfExists(roots, std::wstring(publicProfile) + L"\\Desktop");
        AddJarSearchRootIfExists(roots, std::wstring(publicProfile) + L"\\Downloads");
    }

    ULONGLONG bestTime = 0;
    std::wstring bestText;
    std::wstring bestPath;
    int visited = 0;
    for (const std::wstring& root : roots)
    {
        SearchJarModificationRecursive(root, lowerName, 6, visited, bestTime, bestText, bestPath);
        if (visited > 14000)
            break;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[lowerName] = { bestTime, { bestText, bestPath } };
    }

    if (bestTime == 0 || bestText.empty())
        return false;

    if (outSortTime)
        *outSortTime = bestTime;
    outDate = bestText;
    if (outPath)
        *outPath = bestPath;
    return true;
}

static std::wstring JarFileModificationTimeText(const JarUsnEntry& e, const std::wstring& fullPath, ULONGLONG* outSortTime = nullptr)
{
    if (outSortTime)
        *outSortTime = 0;

    ULONGLONG directTime = 0;
    std::wstring direct = FileLastWriteTimeText(fullPath, &directTime);
    if (!IsUnknownAutomationDate(direct))
    {
        if (outSortTime)
            *outSortTime = directTime;
        return direct;
    }

    std::wstring foundDate;
    ULONGLONG foundTime = 0;
    std::wstring foundPath;
    std::wstring name = ExtractFileNameOnly(fullPath);
    if (name.empty())
        name = ExtractFileNameOnly(e.name);
    if (FindJarModificationByName(name, foundDate, &foundTime, &foundPath))
    {
        if (outSortTime)
            *outSortTime = foundTime;
        return foundDate;
    }

    return L"дата неизвестна";
}

static bool IsUnknownAutomationDate(const std::wstring& text)
{
    std::wstring lower = ToLowerCopy(TrimAutomationField(text));
    return lower.empty() || lower == L"дата неизвестна" || lower == L"n/a" || lower == L"na";
}

static void AppendJarUnifiedActivityRow(std::vector<std::wstring>& lines, const JarUsnEntry& e)
{
    std::wstring reason = SimpleJarJournalReason(e);
    if (reason.empty())
        return;

    std::wstring fullPath = ResolveUsnFullPathForEntry(e, L".jar");
    ULONGLONG modSortTime = 0;
    std::wstring modDate = JarFileModificationTimeText(e, fullPath, &modSortTime);
    std::wstring program = CleanPipeCell(ExtractFileNameOnly(fullPath));
    if (program.empty())
        program = CleanPipeCell(ExtractFileNameOnly(e.name));
    if (program.empty())
        program = CleanPipeCell(e.name);
    if (program.empty())
        return;

    std::wstring reasonCell = BuildJarActivityReasonWithUsnDate(reason, e.date);
    if (reasonCell.empty())
        return;

    std::wstring row = CleanPipeCell(modDate) + L" | " + reasonCell + L" | " + program;
    if (reason == L"deleted")
        row.insert(0, AUTOMATION_DELETED_MARKER);
    lines.push_back(std::move(row));
}

static std::wstring JarUnifiedDedupKey(const JarUsnEntry& e)
{
    std::wstring reason = SimpleJarJournalReason(e);
    std::wstring fullPath = ResolveUsnFullPathForEntry(e, L".jar");
    std::wstring name = ExtractFileNameOnly(fullPath);
    if (name.empty())
        name = ExtractFileNameOnly(e.name);
    if (name.empty())
        name = e.name;

    // Deduplicate by visible file name + visible action. USN can emit many rows
    // for the same command with different references/close events; the user wants
    // one row per same file/action in .jar activity.
    std::wstring key = ToLowerCopy(CleanPipeCell(name)) + L"|" + ToLowerCopy(CleanPipeCell(reason));
    return key;
}

static std::vector<std::wstring> ReadJarUsnEntriesForActivity()
{
    std::vector<std::wstring> rawLines;
    std::wstring output;
    std::wstring command = L"fsutil usn readjournal C: csv | findstr /l /i /c:\".jar\"";
    if (!RunHiddenCommandCapture(command, output, 120000))
    {
        rawLines.push_back(L"__ERROR__");
        return rawLines;
    }
    return SplitAutomationTextLines(output);
}

static std::vector<JarUsnEntry> CollectJarReasonEntries()
{
    std::vector<JarUsnEntry> entries;
    std::vector<std::wstring> raw = ReadJarUsnEntriesForActivity();
    if (raw.size() == 1 && raw[0] == L"__ERROR__")
        return entries;

    entries.reserve(raw.size());
    for (const std::wstring& rawLine : raw)
    {
        if (rawLine.empty())
            continue;
        std::wstring lower = ToLowerCopy(rawLine);
        if (lower.find(L"usn") != std::wstring::npos && lower.find(L"reason") != std::wstring::npos && lower.find(L"name") != std::wstring::npos)
            continue;

        JarUsnEntry e = MakeUsnEntryForExtension(rawLine, L".jar");
        if (!ShouldShowUsnEntryForExtension(e, L".jar"))
            continue;
        if (SimpleJarJournalReason(e).empty())
            continue;
        entries.push_back(std::move(e));
    }
    return entries;
}

static std::vector<std::wstring> ScanJarActivityJournalReasonsOnly()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"дата модификации | действие | программа");

    std::vector<JarUsnEntry> entries = CollectJarReasonEntries();
    if (entries.empty())
    {
        std::vector<std::wstring> probe = ReadJarUsnEntriesForActivity();
        if (probe.size() == 1 && probe[0] == L"__ERROR__")
            lines.push_back(L"не удалось запустить fsutil");
        else
            lines.push_back(L"не найдено");
        return lines;
    }

    std::set<std::wstring> seen;
    for (const JarUsnEntry& e : entries)
    {
        std::wstring key = JarUnifiedDedupKey(e);
        if (!key.empty() && !seen.insert(key).second)
            continue;
        AppendJarUnifiedActivityRow(lines, e);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");
    return lines;
}

static std::vector<std::wstring> ScanUsnActivityForExtension(const std::wstring& ext, bool deletedOnly, bool keepDeletedInActivity = false)
{
    std::vector<std::wstring> lines;
    lines.push_back(L"дата | программа | путь");

    std::wstring output;
    std::wstring command = L"fsutil usn readjournal C: csv | findstr /l /i /c:\"" + ext + L"\"";

    if (!RunHiddenCommandCapture(command, output, 120000))
    {
        lines.push_back(L"не удалось запустить fsutil");
        return lines;
    }

    std::vector<std::wstring> raw = SplitAutomationTextLines(output);
    std::vector<JarUsnEntry> entries;
    entries.reserve(raw.size());

    for (const std::wstring& rawLine : raw)
    {
        if (rawLine.empty())
            continue;

        std::wstring lower = ToLowerCopy(rawLine);
        if (lower.find(L"usn") != std::wstring::npos && lower.find(L"reason") != std::wstring::npos && lower.find(L"name") != std::wstring::npos)
            continue;

        JarUsnEntry entry = MakeUsnEntryForExtension(rawLine, ext);
        if (!entry.name.empty())
            entries.push_back(entry);
    }

    for (const JarUsnEntry& e : entries)
    {
        if (!ShouldShowUsnEntryForExtension(e, ext))
            continue;

        if (deletedOnly)
        {
            // В окнах deleted показываем только те записи USN,
            // где реально есть FILE_DELETE. Один только CLOSE не означает удаление.
            if (e.deleted)
                AppendJarActivityRow(lines, e.date, L"deleted", e.name, ResolveUsnFullPathForEntry(e, ext));
            continue;
        }

        if (e.deleted)
        {
            if (keepDeletedInActivity)
                AppendJarActivityRow(lines, e.date, L"deleted", e.name, ResolveUsnFullPathForEntry(e, ext));
            continue;
        }

        AppendJarActivityRow(lines, e.date, e.reasonText.empty() ? L"activity" : e.reasonText.c_str(), e.name, ResolveUsnFullPathForEntry(e, ext));
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}


static std::vector<std::wstring> SplitTextByPipe(const std::wstring& line)
{
    std::vector<std::wstring> cols;
    size_t start = 0;
    while (start <= line.size())
    {
        size_t pos = line.find(L"|", start);
        std::wstring part = (pos == std::wstring::npos) ? line.substr(start) : line.substr(start, pos - start);
        cols.push_back(TrimAutomationField(part));
        if (pos == std::wstring::npos)
            break;
        start = pos + 1;
    }
    return cols;
}

struct ExeLaunchRegistryRow
{
    std::wstring date;
    std::wstring source;
    std::wstring name;
    std::wstring path;
    ULONGLONG sortTime = 0;
};

static std::wstring RegistryValueTimeText(const FILETIME& ft, ULONGLONG& sortTime)
{
    sortTime = 0;
    if (!IsPlausibleFileTime(ft))
        return L"дата неизвестна";
    sortTime = FileTimeToUInt64(ft);
    return FileTimeToLocalText(ft);
}

static bool ExtractExePathFromRegistryText(const std::wstring& text, std::wstring& outPath)
{
    std::wstring cleaned = TrimAutomationField(text);
    if (cleaned.empty())
        return false;

    size_t exePos = ToLowerCopy(cleaned).find(L".exe");
    if (exePos == std::wstring::npos)
        return false;

    size_t end = exePos + 4;
    size_t start = cleaned.rfind(L'\"', exePos);
    if (start != std::wstring::npos)
    {
        start += 1;
        outPath = cleaned.substr(start, end - start);
        return !outPath.empty();
    }

    start = cleaned.rfind(L"C:\\", exePos);
    if (start == std::wstring::npos)
        start = cleaned.rfind(L"D:\\", exePos);
    if (start == std::wstring::npos)
        start = cleaned.rfind(L"E:\\", exePos);
    if (start == std::wstring::npos)
        start = cleaned.rfind(L"\\Device\\", exePos);
    if (start == std::wstring::npos)
        start = 0;

    outPath = cleaned.substr(start, end - start);
    while (!outPath.empty() && (outPath.front() == L' ' || outPath.front() == L'\t' || outPath.front() == L'\"'))
        outPath.erase(outPath.begin());
    while (!outPath.empty() && (outPath.back() == L' ' || outPath.back() == L'\t' || outPath.back() == L'\"'))
        outPath.pop_back();
    return !outPath.empty();
}

static std::wstring Rot13Wide(const std::wstring& text)
{
    std::wstring out = text;
    for (wchar_t& ch : out)
    {
        if (ch >= L'a' && ch <= L'z')
            ch = static_cast<wchar_t>(L'a' + ((ch - L'a' + 13) % 26));
        else if (ch >= L'A' && ch <= L'Z')
            ch = static_cast<wchar_t>(L'A' + ((ch - L'A' + 13) % 26));
    }
    return out;
}

static void AddExeLaunchRow(std::vector<ExeLaunchRegistryRow>& rows, const std::wstring& source, const std::wstring& rawPath, const std::wstring& fallbackDate, ULONGLONG sortTime)
{
    std::wstring path = DevicePathToDosPath(rawPath);
    path = TrimAutomationField(path);

    std::wstring lower = ToLowerCopy(path);
    if (lower.find(L".exe") == std::wstring::npos)
        return;

    // Системные exe не добавляем в .exe activity ни из реестра, ни из Prefetch.
    if (IsLikelySystemExecutable(path))
        return;

    ExeLaunchRegistryRow row;
    row.date = fallbackDate.empty() ? L"дата неизвестна" : fallbackDate;
    row.source = source;
    row.path = path;
    row.name = ExtractFileNameOnly(path);
    if (row.name.empty())
        row.name = path;
    row.sortTime = sortTime;
    rows.push_back(std::move(row));
}

static bool IsPrefetchLaunchRow(const ExeLaunchRegistryRow& row)
{
    return ToLowerCopy(row.source).find(L"prefetch") != std::wstring::npos;
}


struct PrefetchLaunchInfo
{
    std::wstring exeName;
    std::wstring path;
    std::wstring date;
    ULONGLONG sortTime = 0;
};

static std::wstring NormalizeExeLaunchKey(const std::wstring& text)
{
    std::wstring value = ToLowerCopy(DevicePathToDosPath(TrimAutomationField(text)));
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return value;
}

static bool SameExeLaunchPathOrName(const ExeLaunchRegistryRow& row, const PrefetchLaunchInfo& pf)
{
    std::wstring rowPath = NormalizeExeLaunchKey(row.path);
    std::wstring pfPath = NormalizeExeLaunchKey(pf.path);
    if (!rowPath.empty() && !pfPath.empty() && rowPath == pfPath)
        return true;

    std::wstring rowName = ToLowerCopy(row.name.empty() ? ExtractFileNameOnly(row.path) : row.name);
    std::wstring pfName = ToLowerCopy(pf.exeName.empty() ? ExtractFileNameOnly(pf.path) : pf.exeName);
    return !rowName.empty() && !pfName.empty() && rowName == pfName;
}

static bool ApplyPrefetchTimeToRegistryRow(ExeLaunchRegistryRow& row, const std::vector<PrefetchLaunchInfo>& prefetch)
{
    const PrefetchLaunchInfo* best = nullptr;
    for (const PrefetchLaunchInfo& pf : prefetch)
    {
        if (!SameExeLaunchPathOrName(row, pf))
            continue;
        if (!best || pf.sortTime > best->sortTime)
            best = &pf;
    }

    if (!best || best->sortTime == 0)
        return false;

    row.date = best->date;
    row.sortTime = best->sortTime;
    if (ToLowerCopy(row.source).find(L"prefetch") == std::wstring::npos)
        row.source += L" / Prefetch time";
    return true;
}

static std::wstring PrefetchExeNameFromFileName(const std::wstring& fileName)
{
    std::wstring name = ExtractFileNameOnly(fileName);
    std::wstring lower = ToLowerCopy(name);

    if (lower.size() < 3 || lower.substr(lower.size() - 3) != L".pf")
        return L"";

    name = name.substr(0, name.size() - 3);

    size_t dash = name.find_last_of(L'-');
    if (dash != std::wstring::npos && dash > 0)
        name = name.substr(0, dash);

    name = TrimAutomationField(name);
    if (name.empty())
        return L"";

    if (ToLowerCopy(name).find(L".exe") == std::wstring::npos)
        name += L".exe";

    return name;
}

static bool IsReadablePrefetchChar(wchar_t ch)
{
    return ch >= 32 && ch < 127 || ch >= 0x0400 && ch <= 0x052F;
}

static bool ReadWholeFileBytes(const std::wstring& path, std::vector<BYTE>& bytes)
{
    bytes.clear();

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 32LL * 1024LL * 1024LL)
    {
        CloseHandle(h);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(h);

    if (!ok || read == 0)
    {
        bytes.clear();
        return false;
    }

    bytes.resize(read);
    return true;
}

static bool IsBetterPrefetchExePathCandidate(const std::wstring& candidate, const std::wstring& currentBest)
{
    if (currentBest.empty())
        return true;

    std::wstring c = ToLowerCopy(candidate);
    std::wstring b = ToLowerCopy(currentBest);

    bool cDos = c.size() > 2 && c[1] == L':' && (c[2] == L'\\' || c[2] == L'/');
    bool bDos = b.size() > 2 && b[1] == L':' && (b[2] == L'\\' || b[2] == L'/');
    if (cDos != bDos)
        return cDos;

    bool cDevice = c.rfind(L"\\device\\", 0) == 0;
    bool bDevice = b.rfind(L"\\device\\", 0) == 0;
    if (cDevice != bDevice)
        return cDevice;

    return candidate.size() > currentBest.size();
}

static bool TryExtractExePathFromPrefetchFile(const std::wstring& pfPath, const std::wstring& exeName, std::wstring& outPath)
{
    outPath.clear();

    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(pfPath, bytes) || bytes.size() < sizeof(wchar_t))
        return false;

    std::wstring target = ToLowerCopy(ExtractFileNameOnly(exeName));
    std::wstring current;
    std::wstring best;

    auto flush = [&]()
    {
        if (current.size() >= 5)
        {
            std::wstring lower = ToLowerCopy(current);
            if (lower.find(L".exe") != std::wstring::npos &&
                (target.empty() || ToLowerCopy(ExtractFileNameOnly(current)) == target || lower.find(target) != std::wstring::npos))
            {
                std::wstring candidate;
                if (ExtractExePathFromRegistryText(current, candidate))
                {
                    candidate = DevicePathToDosPath(candidate);
                    if (!IsLikelySystemExecutable(candidate) && IsBetterPrefetchExePathCandidate(candidate, best))
                        best = candidate;
                }
            }
        }
        current.clear();
    };

    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
    {
        wchar_t ch = static_cast<wchar_t>(bytes[i] | (static_cast<WORD>(bytes[i + 1]) << 8));
        if (IsReadablePrefetchChar(ch))
        {
            current.push_back(ch);
            if (current.size() > 1024)
                flush();
        }
        else
        {
            flush();
        }
    }
    flush();

    if (best.empty())
        return false;

    outPath = best;
    return true;
}


static std::vector<PrefetchLaunchInfo> BuildPrefetchLaunchInfos()
{
    std::vector<PrefetchLaunchInfo> infos;

    wchar_t windowsDir[MAX_PATH * 4]{};
    UINT len = GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir));
    if (len == 0 || len >= ARRAYSIZE(windowsDir))
        return infos;

    std::wstring folder(windowsDir);
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/'))
        folder.pop_back();
    folder += L"\\Prefetch";

    std::wstring mask = folder + L"\\*.pf";
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW(mask.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return infos;

    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::wstring exeName = PrefetchExeNameFromFileName(data.cFileName);
        if (exeName.empty())
            continue;

        // Системные exe не добавляем даже как источник времени для .exe activity.
        if (IsLikelySystemExecutable(exeName))
            continue;

        ULONGLONG sortTime = 0;
        std::wstring when = L"дата неизвестна";
        if (IsPlausibleFileTime(data.ftLastWriteTime))
        {
            sortTime = FileTimeToUInt64(data.ftLastWriteTime);
            when = FileTimeToLocalText(data.ftLastWriteTime);
        }

        std::wstring pfPath = folder + L"\\" + data.cFileName;
        std::wstring exePath;
        TryExtractExePathFromPrefetchFile(pfPath, exeName, exePath);

        if (!exePath.empty() && IsLikelySystemExecutable(exePath))
            continue;

        PrefetchLaunchInfo info;
        info.exeName = exeName;
        info.path = exePath;
        info.date = when;
        info.sortTime = sortTime;
        infos.push_back(std::move(info));
    }
    while (FindNextFileW(h, &data));

    FindClose(h);
    return infos;
}

static void AppendPrefetchExeLaunchRows(std::vector<ExeLaunchRegistryRow>& rows)
{
    wchar_t windowsDir[MAX_PATH * 4]{};
    UINT len = GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir));
    if (len == 0 || len >= ARRAYSIZE(windowsDir))
        return;

    std::wstring folder(windowsDir);
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/'))
        folder.pop_back();
    folder += L"\\Prefetch";

    std::wstring mask = folder + L"\\*.pf";
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW(mask.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::wstring exeName = PrefetchExeNameFromFileName(data.cFileName);
        if (exeName.empty())
            continue;

        std::wstring pfPath = folder + L"\\" + data.cFileName;
        std::wstring exePath;

        // Из Prefetch берём не только имя .pf, а пытаемся достать реальный путь запущенного exe из содержимого .pf.
        // Если путь восстановить нельзя, строку не добавляем, чтобы в .exe activity не было записей без пути.
        if (!TryExtractExePathFromPrefetchFile(pfPath, exeName, exePath))
            continue;

        ULONGLONG sortTime = 0;
        std::wstring when = L"дата неизвестна";
        if (IsPlausibleFileTime(data.ftLastWriteTime))
        {
            sortTime = FileTimeToUInt64(data.ftLastWriteTime);
            when = FileTimeToLocalText(data.ftLastWriteTime);
        }

        AddExeLaunchRow(rows, L"Prefetch / modified", exePath, when, sortTime);
    }
    while (FindNextFileW(h, &data));

    FindClose(h);
}

static void AppendMuiCacheValues(HKEY root, const std::wstring& subKey, const std::wstring& label, std::vector<ExeLaunchRegistryRow>& rows)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS &&
        RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS)
        return;

    DWORD valueCount = 0, maxValueNameLen = 0, maxValueDataLen = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr);
    ULONGLONG sortTime = 0;
    std::wstring when;

    for (DWORD i = 0; i < valueCount; ++i)
    {
        std::vector<wchar_t> valueName(maxValueNameLen + 2, L'\0');
        std::vector<BYTE> valueData(std::max<DWORD>(maxValueDataLen + 4, 8));
        DWORD nameLen = static_cast<DWORD>(valueName.size());
        DWORD dataLen = static_cast<DWORD>(valueData.size());
        DWORD type = 0;
        if (RegEnumValueW(key, i, valueName.data(), &nameLen, nullptr, &type, valueData.data(), &dataLen) != ERROR_SUCCESS)
            continue;

        std::wstring raw(valueName.data(), nameLen);
        std::wstring path;
        if (ExtractExePathFromRegistryText(raw, path))
            AddExeLaunchRow(rows, label + L" / MuiCache", path, when, sortTime);
    }

    RegCloseKey(key);
}

static void AppendRegistryExeValueNames(HKEY root, const std::wstring& subKey, const std::wstring& label, bool userAssistRot13, std::vector<ExeLaunchRegistryRow>& rows)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS &&
        RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS)
        return;

    DWORD subKeyCount = 0, maxSubKeyLen = 0, valueCount = 0, maxValueNameLen = 0, maxValueDataLen = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKeyLen, nullptr, &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr);
    ULONGLONG sortTime = 0;
    std::wstring when;

    for (DWORD i = 0; i < valueCount; ++i)
    {
        std::vector<wchar_t> valueName(maxValueNameLen + 2, L'\0');
        std::vector<BYTE> valueData(std::max<DWORD>(maxValueDataLen + 4, 8));
        DWORD nameLen = static_cast<DWORD>(valueName.size());
        DWORD dataLen = static_cast<DWORD>(valueData.size());
        DWORD type = 0;
        if (RegEnumValueW(key, i, valueName.data(), &nameLen, nullptr, &type, valueData.data(), &dataLen) != ERROR_SUCCESS)
            continue;

        std::wstring text(valueName.data(), nameLen);
        if (userAssistRot13)
            text = Rot13Wide(text);

        std::wstring path;
        if (ExtractExePathFromRegistryText(text, path))
            AddExeLaunchRow(rows, label, path, when, sortTime);
    }

    for (DWORD i = 0; i < subKeyCount; ++i)
    {
        std::vector<wchar_t> child(maxSubKeyLen + 2, L'\0');
        DWORD childLen = static_cast<DWORD>(child.size());
        if (RegEnumKeyExW(key, i, child.data(), &childLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && childLen > 0)
            AppendRegistryExeValueNames(root, subKey + L"\\" + std::wstring(child.data(), childLen), label, userAssistRot13, rows);
    }

    RegCloseKey(key);
}

static void AppendRegistryExeValueData(HKEY root, const std::wstring& subKey, const std::wstring& label, std::vector<ExeLaunchRegistryRow>& rows)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS &&
        RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS)
        return;

    DWORD subKeyCount = 0, maxSubKeyLen = 0, valueCount = 0, maxValueNameLen = 0, maxValueDataLen = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKeyLen, nullptr, &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr);
    ULONGLONG sortTime = 0;
    std::wstring when;

    for (DWORD i = 0; i < valueCount; ++i)
    {
        std::vector<wchar_t> valueName(maxValueNameLen + 2, L'\0');
        std::vector<BYTE> valueData(std::max<DWORD>(maxValueDataLen + 4, 8));
        DWORD nameLen = static_cast<DWORD>(valueName.size());
        DWORD dataLen = static_cast<DWORD>(valueData.size());
        DWORD type = 0;
        if (RegEnumValueW(key, i, valueName.data(), &nameLen, nullptr, &type, valueData.data(), &dataLen) != ERROR_SUCCESS)
            continue;

        std::wstring text;
        if ((type == REG_SZ || type == REG_EXPAND_SZ) && dataLen >= sizeof(wchar_t))
            text.assign(reinterpret_cast<wchar_t*>(valueData.data()), dataLen / sizeof(wchar_t));
        else
            text.assign(valueName.data(), nameLen);

        std::wstring path;
        if (ExtractExePathFromRegistryText(text, path))
            AddExeLaunchRow(rows, label, path, when, sortTime);
    }

    for (DWORD i = 0; i < subKeyCount; ++i)
    {
        std::vector<wchar_t> child(maxSubKeyLen + 2, L'\0');
        DWORD childLen = static_cast<DWORD>(child.size());
        if (RegEnumKeyExW(key, i, child.data(), &childLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && childLen > 0)
            AppendRegistryExeValueData(root, subKey + L"\\" + std::wstring(child.data(), childLen), label, rows);
    }

    RegCloseKey(key);
}

static void AppendMuiCacheForAllUsers(std::vector<ExeLaunchRegistryRow>& rows)
{
    const std::wstring muiSub = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache";
    AppendMuiCacheValues(HKEY_CLASSES_ROOT, L"Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache", L"HKCR", rows);
    AppendMuiCacheValues(HKEY_CURRENT_USER, muiSub, L"HKCU", rows);

    HKEY users = nullptr;
    if (RegOpenKeyExW(HKEY_USERS, nullptr, 0, KEY_READ, &users) != ERROR_SUCCESS)
        return;

    DWORD subKeyCount = 0, maxSubKeyLen = 0;
    if (RegQueryInfoKeyW(users, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        for (DWORD i = 0; i < subKeyCount; ++i)
        {
            std::vector<wchar_t> sid(maxSubKeyLen + 2, L'\0');
            DWORD sidLen = static_cast<DWORD>(sid.size());
            if (RegEnumKeyExW(users, i, sid.data(), &sidLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS || sidLen == 0)
                continue;

            std::wstring sidText(sid.data(), sidLen);
            AppendMuiCacheValues(HKEY_USERS, sidText + L"\\" + muiSub, L"HKU\\" + ShortSidTail(sidText), rows);
            AppendMuiCacheValues(HKEY_USERS, sidText + L"_Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache", L"HKU\\" + ShortSidTail(sidText) + L"_Classes", rows);
        }
    }

    RegCloseKey(users);
}

static void AppendExeLaunchArtifactsForUserRoot(HKEY root, const std::wstring& prefix, const std::wstring& labelPrefix, std::vector<ExeLaunchRegistryRow>& rows)
{
    std::wstring base = prefix.empty() ? L"" : prefix + L"\\";
    AppendRegistryExeValueNames(root, base + L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist", labelPrefix + L"UserAssist", true, rows);
    AppendRegistryExeValueData(root, base + L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Store", labelPrefix + L"Compatibility Assistant Store", rows);
    AppendRegistryExeValueData(root, base + L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Persisted", labelPrefix + L"Compatibility Assistant Persisted", rows);
    AppendRegistryExeValueData(root, base + L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", labelPrefix + L"AppCompat Layers", rows);
    AppendRegistryExeValueData(root, base + L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU", labelPrefix + L"RunMRU", rows);
    AppendRegistryExeValueData(root, base + L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32\\OpenSavePidlMRU", labelPrefix + L"OpenSavePidlMRU", rows);
    AppendRegistryExeValueData(root, base + L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs", labelPrefix + L"RecentDocs", rows);
}

static void AppendExeLaunchArtifactsForAllUsers(std::vector<ExeLaunchRegistryRow>& rows)
{
    AppendExeLaunchArtifactsForUserRoot(HKEY_CURRENT_USER, L"", L"HKCU / ", rows);
    AppendExeLaunchArtifactsForUserRoot(HKEY_LOCAL_MACHINE, L"", L"HKLM / ", rows);

    HKEY users = nullptr;
    if (RegOpenKeyExW(HKEY_USERS, nullptr, 0, KEY_READ, &users) != ERROR_SUCCESS)
        return;

    DWORD subKeyCount = 0, maxSubKeyLen = 0;
    if (RegQueryInfoKeyW(users, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        for (DWORD i = 0; i < subKeyCount; ++i)
        {
            std::vector<wchar_t> sid(maxSubKeyLen + 2, L'\0');
            DWORD sidLen = static_cast<DWORD>(sid.size());
            if (RegEnumKeyExW(users, i, sid.data(), &sidLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS || sidLen == 0)
                continue;

            std::wstring sidText(sid.data(), sidLen);
            if (sidText.find(L"_Classes") != std::wstring::npos)
                continue;
            AppendExeLaunchArtifactsForUserRoot(HKEY_USERS, sidText, L"HKU\\" + ShortSidTail(sidText) + L" / ", rows);
        }
    }

    RegCloseKey(users);
}

static void AppendBamExeLaunchRows(std::vector<ExeLaunchRegistryRow>& rows)
{
    std::vector<BamEntry> bamRows;
    std::vector<std::wstring> notes;
    const wchar_t* subPaths[] =
    {
        L"SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings",
        L"SYSTEM\\CurrentControlSet\\Services\\bam\\UserSettings",
        L"SYSTEM\\ControlSet001\\Services\\bam\\State\\UserSettings",
        L"SYSTEM\\ControlSet001\\Services\\bam\\UserSettings",
        L"SYSTEM\\ControlSet002\\Services\\bam\\State\\UserSettings",
        L"SYSTEM\\ControlSet002\\Services\\bam\\UserSettings"
    };

    for (const wchar_t* subPath : subPaths)
        AppendBamRegistryKey(subPath, true, bamRows, notes);

    for (const BamEntry& entry : bamRows)
    {
        if (entry.signature != BamSignatureState::NotSigned)
            continue;

        std::wstring raw = entry.path.empty() ? entry.exeName : entry.path;
        AddExeLaunchRow(rows, L"BAM / unsigned", raw, entry.when, entry.fileTimeValue);
    }
}

static std::vector<std::wstring> ScanExeLaunchesFromRegistry()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"дата запуска | программа | путь");

    std::vector<ExeLaunchRegistryRow> rows;

    // .exe activity снова сканирует реестр, но НЕ берёт дату через RegQueryInfoKeyW(..., &keyWrite).
    // Реестр здесь используется как источник путей/факта активности, а время запуска берётся из Prefetch modified time.
    std::vector<PrefetchLaunchInfo> prefetch = BuildPrefetchLaunchInfos();

    AppendPrefetchExeLaunchRows(rows);

    std::vector<ExeLaunchRegistryRow> registryRows;
    AppendExeLaunchArtifactsForAllUsers(registryRows);
    AppendMuiCacheForAllUsers(registryRows);
    for (ExeLaunchRegistryRow& regRow : registryRows)
    {
        // Без совпадения с Prefetch не показываем строку: иначе дата была бы не временем запуска конкретного exe.
        if (ApplyPrefetchTimeToRegistryRow(regRow, prefetch))
            rows.push_back(std::move(regRow));
    }

    // BAM имеет отдельный FILETIME у значения, поэтому его можно использовать как источник времени.
    // В .exe activity из BAM выводим только unsigned/not signed.
    AppendBamExeLaunchRows(rows);

    // Сначала убираем дубли одной программы, потом сортируем строго по времени.
    // Старый вариант сортировал Prefetch внутри компаратора и из-за этого порядок мог быть "разнобой".
    std::vector<ExeLaunchRegistryRow> uniqueRows;
    for (const ExeLaunchRegistryRow& row : rows)
    {
        std::wstring key = ToLowerCopy(row.name);
        if (key.empty())
            key = ToLowerCopy(row.path);
        if (key.empty())
            continue;

        auto it = std::find_if(uniqueRows.begin(), uniqueRows.end(), [&](const ExeLaunchRegistryRow& existing)
        {
            std::wstring existingKey = ToLowerCopy(existing.name);
            if (existingKey.empty())
                existingKey = ToLowerCopy(existing.path);
            return existingKey == key;
        });

        if (it == uniqueRows.end())
        {
            uniqueRows.push_back(row);
            continue;
        }

        bool rowPrefetch = IsPrefetchLaunchRow(row);
        bool oldPrefetch = IsPrefetchLaunchRow(*it);

        if ((rowPrefetch && row.sortTime > 0 && !oldPrefetch) ||
            (row.sortTime > it->sortTime) ||
            (row.sortTime == it->sortTime && rowPrefetch && !oldPrefetch))
        {
            *it = row;
        }
    }

    rows.swap(uniqueRows);

    std::sort(rows.begin(), rows.end(), [](const ExeLaunchRegistryRow& a, const ExeLaunchRegistryRow& b)
    {
        if (a.sortTime != b.sortTime)
            return a.sortTime > b.sortTime;

        bool au = (a.sortTime == 0);
        bool bu = (b.sortTime == 0);
        if (au != bu)
            return !au;

        int p = _wcsicmp(a.date.c_str(), b.date.c_str());
        if (p != 0)
            return p > 0;

        p = _wcsicmp(a.path.c_str(), b.path.c_str());
        if (p != 0)
            return p < 0;

        return _wcsicmp(a.source.c_str(), b.source.c_str()) < 0;
    });

    for (const ExeLaunchRegistryRow& row : rows)
    {
        std::wstring cleanPath = CleanPipeCell(DevicePathToDosPath(row.path.empty() ? row.name : row.path));
        std::wstring rawProgram = row.name.empty() ? ExtractFileNameOnly(cleanPath) : row.name;
        std::wstring program = CleanPipeCell(ExtractFileNameOnly(rawProgram));
        if (program.empty())
            program = ExtractFileNameOnly(cleanPath);
        if (program.empty() || cleanPath.empty())
            continue;
        lines.push_back(CleanPipeCell(row.date) + L" | " + program + L" | " + cleanPath);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}

static std::vector<std::wstring> ScanExeDeletedFromUsnDeleteClose()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"дата | программа | путь");

    std::wstring output;
    std::wstring command = L"fsutil usn readjournal C: csv | findstr /l /i /c:\".exe\"";
    if (!RunHiddenCommandCapture(command, output, 120000))
    {
        lines.push_back(L"не удалось запустить fsutil");
        return lines;
    }

    std::vector<std::wstring> raw = SplitAutomationTextLines(output);
    for (const std::wstring& rawLine : raw)
    {
        if (rawLine.empty())
            continue;

        std::wstring lower = ToLowerCopy(rawLine);
        if (lower.find(L"usn") != std::wstring::npos && lower.find(L"reason") != std::wstring::npos && lower.find(L"name") != std::wstring::npos)
            continue;

        JarUsnEntry e = MakeUsnEntryForExtension(rawLine, L".exe");
        if (!ShouldShowUsnEntryForExtension(e, L".exe"))
            continue;

        // Только FILE_DELETE считается подтверждённым удалением.
        // CLOSE без FILE_DELETE больше не выводится в deleted.
        if (e.deleted)
            AppendJarActivityRow(lines, e.date, L"deleted", e.name, ResolveUsnFullPathForEntry(e, L".exe"));
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}


static std::vector<std::wstring> ScanExeActivityWithDeleted()
{
    std::vector<std::wstring> lines = ScanExeLaunchesFromRegistry();

    // .exe deleted больше не отдельное окно: добавляем подтверждённые удаления в .exe activity.
    std::vector<std::wstring> deleted = ScanExeDeletedFromUsnDeleteClose();

    if (lines.size() == 2 && TrimAutomationField(lines[1]) == L"не найдено")
        lines.pop_back();

    for (size_t i = 1; i < deleted.size(); ++i)
    {
        std::wstring row = TrimAutomationField(deleted[i]);
        if (row.empty() || row == L"не найдено")
            continue;
        lines.push_back(row);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}

static bool LooksLikeMinecraftLogFileNameStrict(const std::wstring& name)
{
    std::wstring n = ToLowerCopy(ExtractFileNameOnly(name));
    if (n == L"latest.log" || n == L"debug.log" || n.find(L"hs_err_pid") != std::wstring::npos)
        return true;
    if (n.size() >= 14 && n[4] == L'-' && n[7] == L'-' && n.find(L".log") != std::wstring::npos)
        return true;
    return n.find(L".log.gz") != std::wstring::npos;
}

static bool RawUsnLineLooksLikeMinecraftLogsActivity(const std::wstring& rawLine)
{
    std::wstring lower = ToLowerCopy(rawLine);
    bool hasPath = (lower.find(L".minecraft") != std::wstring::npos && lower.find(L"logs") != std::wstring::npos) ||
        (lower.find(L"minecraft\\game") != std::wstring::npos && lower.find(L"logs") != std::wstring::npos) ||
        (lower.find(L"minecraft/game") != std::wstring::npos && lower.find(L"logs") != std::wstring::npos);

    if (hasPath)
        return lower.find(L".log") != std::wstring::npos || lower.find(L"log.gz") != std::wstring::npos ||
            lower.find(L"latest") != std::wstring::npos || lower.find(L"debug") != std::wstring::npos ||
            lower.find(L"hs_err_pid") != std::wstring::npos;

    return lower.find(L"latest.log") != std::wstring::npos ||
        lower.find(L"debug.log") != std::wstring::npos ||
        lower.find(L"hs_err_pid") != std::wstring::npos ||
        (lower.find(L".log") != std::wstring::npos && lower.find(L"-") != std::wstring::npos);
}

static std::wstring PickMinecraftLogActivityName(const std::wstring& rawLine)
{
    std::vector<std::wstring> fields = ParseAutomationCsvLine(rawLine);

    for (const std::wstring& field : fields)
    {
        std::wstring candidate = ExtractFileNameOnly(TrimAutomationField(field));
        if (LooksLikeMinecraftLogFileNameStrict(candidate))
            return candidate;
    }

    std::wstring lower = ToLowerCopy(rawLine);
    const wchar_t* tokens[] = { L"latest.log", L"debug.log", L".log.gz", L".log", L"hs_err_pid" };
    size_t pos = std::wstring::npos;
    size_t tokenLen = 0;
    for (const wchar_t* token : tokens)
    {
        size_t p = lower.find(token);
        if (p != std::wstring::npos && (pos == std::wstring::npos || p < pos))
        {
            pos = p;
            tokenLen = wcslen(token);
        }
    }
    if (pos == std::wstring::npos)
        return L"";

    size_t start = pos;
    while (start > 0)
    {
        wchar_t ch = rawLine[start - 1];
        if (ch == L',' || ch == L'"' || ch == L'|' || ch == L' ' || ch == L'\t' || ch == L'\\' || ch == L'/')
            break;
        --start;
    }

    size_t end = pos + tokenLen;
    while (end < rawLine.size())
    {
        wchar_t ch = rawLine[end];
        if (ch == L',' || ch == L'"' || ch == L'|' || ch == L' ' || ch == L'\t')
            break;
        ++end;
    }

    std::wstring name = ExtractFileNameOnly(rawLine.substr(start, end - start));
    return LooksLikeMinecraftLogFileNameStrict(name) ? name : L"";
}

static std::vector<std::wstring> ScanMinecraftLogsFolders()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"дата | действие | файл");

    std::wstring script = LR"HCPS(
$ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
function SplitCsvLine([string]$line){
  $list = New-Object System.Collections.Generic.List[string]
  $sb = New-Object System.Text.StringBuilder
  $q = $false
  for($i=0; $i -lt $line.Length; $i++){
    $ch = $line[$i]
    if($ch -eq '"'){
      if($q -and ($i + 1) -lt $line.Length -and $line[$i+1] -eq '"'){[void]$sb.Append('"'); $i++} else {$q = -not $q}
    } elseif($ch -eq ',' -and -not $q){$list.Add($sb.ToString()); [void]$sb.Clear()}
    else {[void]$sb.Append($ch)}
  }
  $list.Add($sb.ToString())
  return @($list)
}
function IsMcLogName([string]$n){
  if([string]::IsNullOrWhiteSpace($n)){return $false}
  $x=[IO.Path]::GetFileName($n.Trim(' "')).ToLowerInvariant()
  return ($x -eq 'latest.log' -or $x -eq 'debug.log' -or $x -like '*.log.gz' -or $x -like '*.log' -or $x -like 'hs_err_pid*')
}
function ActionFromReason([string]$reason){
  $r=$reason.ToLowerInvariant()
  if($r -match 'rename.*old|old name|0x00001000'){return 'rename old'}
  if($r -match 'rename.*new|new name|0x00002000'){return 'rename new'}
  if($r -match 'file delete|file_delete|0x00000200'){return 'deleted'}
  if($r -match 'file create|file_create|0x00000100'){return 'created'}
  if($r -match 'data extend|data_extend|0x00000002'){return 'data extend'}
  if($r -match 'basic info|basic_info|0x00008000'){return 'basic info'}
  if($r -match 'stream change|stream_change|0x00200000'){return 'stream change'}
  return 'activity'
}
$raw = cmd /d /c 'fsutil usn readjournal C: csv 2^>nul ^| findstr /i /c:"latest.log" /c:"debug.log" /c:".log.gz" /c:"hs_err_pid"'
if($LASTEXITCODE -ne 0 -or $null -eq $raw){exit}
$seen=@{}
foreach($line in @($raw)){
  if([string]::IsNullOrWhiteSpace($line)){continue}
  if($line -match 'Reason' -and $line -match 'Name'){continue}
  $fields=SplitCsvLine $line
  $name=''
  foreach($f in $fields){ if(IsMcLogName $f){$name=[IO.Path]::GetFileName($f.Trim(' "')); break} }
  if(-not (IsMcLogName $name)){continue}
  $lower=$line.ToLowerInvariant()
  if(($lower -match '\\' -or $lower -match '/') -and -not (($lower -match '\\.minecraft\\.*\\logs\\') -or ($lower -match '\\minecraft\\game\\.*\\logs\\') -or ($lower -match '\\minecraft\\game\\logs\\'))){
    if($lower -match 'c:\\|d:\\|e:\\'){continue}
  }
  $date='-'
  foreach($f in $fields){ if($f -match '\d{1,2}[\.\-/]\d{1,2}[\.\-/]\d{2,4}.*\d{1,2}:\d{2}'){$date=$f.Trim(' "'); break} }
  $row='{0} | {1} | {2}' -f $date,(ActionFromReason ($fields -join ' ')),$name
  if(-not $seen.ContainsKey($row)){$seen[$row]=1; $row}
}
)HCPS";

    std::wstring output;
    if (!RunHiddenPowerShellScriptCapture(script, output, 120000))
    {
        lines.push_back(L"не удалось запустить fsutil");
        return lines;
    }

    std::vector<std::wstring> raw = SplitAutomationTextLines(output);
    for (const std::wstring& line : raw)
    {
        if (!line.empty() && ToLowerCopy(line).find(L"filename, directory name, or volume label") == std::wstring::npos)
            AppendWrappedAutomationLine(lines, line, 170);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");
    return lines;
}

static std::vector<std::wstring> ScanActivityByMode(int mode)
{
    if (mode == 1)
        return ScanUsnActivityForExtension(L".exe", false);
    if (mode == 2)
        return ScanUsnActivityForExtension(L".pf", true);
    if (mode == 3)
        return ScanMinecraftLogsFolders();
    return ScanUsnActivityForExtension(L".jar", false);
}

static std::vector<std::wstring> ScanModrinthHashes()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"название | путь");

    std::wstring script = LR"HCPS(
$ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
try{[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12}catch{}
try{Add-Type -AssemblyName System.IO.Compression.FileSystem}catch{}
function AddRoot([string]$p){ if(-not [string]::IsNullOrWhiteSpace($p) -and (Test-Path -LiteralPath $p)){ $script:roots += (Resolve-Path -LiteralPath $p).Path } }
function IsAllowedPath([string]$p){
  if([string]::IsNullOrWhiteSpace($p)){return $false}
  if($p -match '^C:\\(Windows|Program Files|Program Files \(x86\)|ProgramData|Recovery|System Volume Information|\$Recycle\.Bin)(\\|$)'){return $false}
  if($p -match '\\(jdk|jre|java|javapath|libraries|assets|runtime|versions)\\'){return $false}
  if($p -match '\\.minecraft\\(libraries|assets|runtime|versions)\\'){return $false}
  if($p -match '\\.tlauncher\\legacy\\Minecraft\\game\\(libraries|assets|runtime|versions)\\'){return $false}
  return $true
}
function AddJarItem([string]$name,[string]$path,[string]$hash){
  if([string]::IsNullOrWhiteSpace($hash)){return}
  $script:items += [PSCustomObject]@{Hash=$hash.ToLowerInvariant();Name=$name;Path=$path}
}
function Find7Zip(){
  $c=@('C:\Program Files\7-Zip\7z.exe','C:\Program Files (x86)\7-Zip\7z.exe')
  foreach($x in $c){if(Test-Path -LiteralPath $x){return $x}}
  try{$cmd=Get-Command 7z.exe -ErrorAction SilentlyContinue; if($cmd){return $cmd.Source}}catch{}
  return $null
}
$roots=@()
if(Test-Path -LiteralPath 'C:\Users'){
  Get-ChildItem -LiteralPath 'C:\Users' -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
    AddRoot (Join-Path $_.FullName 'Downloads')
    AddRoot (Join-Path $_.FullName 'Desktop')
    AddRoot (Join-Path $_.FullName 'Documents')
    AddRoot (Join-Path $_.FullName 'AppData\Roaming\.minecraft\mods')
    AddRoot (Join-Path $_.FullName 'AppData\Roaming\.tlauncher\legacy\Minecraft\game\mods')
    AddRoot (Join-Path $_.FullName 'AppData\Roaming\PrismLauncher\instances')
    AddRoot (Join-Path $_.FullName 'curseforge\minecraft\Instances')
  }
}
$roots=@($roots | Where-Object {$_} | Select-Object -Unique)
if($roots.Count -eq 0){'mods folder not found'; exit}
$items=@()
$seen=@{}
foreach($r in $roots){
  try{
    Get-ChildItem -LiteralPath $r -Filter '*.jar' -File -Recurse -Force -ErrorAction SilentlyContinue | Where-Object { IsAllowedPath $_.FullName } | ForEach-Object {
      $k=$_.FullName.ToLowerInvariant(); if(-not $seen.ContainsKey($k)){ $seen[$k]=1; try{AddJarItem $_.Name $_.FullName (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA512).Hash}catch{} }
    }
  }catch{}
}
$archives=@{}
foreach($r in $roots){
  try{
    Get-ChildItem -LiteralPath $r -File -Recurse -Force -ErrorAction SilentlyContinue -Include '*.zip','*.rar','*.7z' | Where-Object { IsAllowedPath $_.FullName } | ForEach-Object { $archives[$_.FullName.ToLowerInvariant()]=$_ }
  }catch{}
}
$tempRoot=Join-Path $env:TEMP ('HolyCheck_checkmods_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
try{
  foreach($a in @($archives.Values)){
    $ext=[IO.Path]::GetExtension($a.FullName).ToLowerInvariant()
    if($ext -eq '.zip'){
      try{
        $zip=[IO.Compression.ZipFile]::OpenRead($a.FullName)
        foreach($e in $zip.Entries){
          if($e.FullName -match '\.jar$' -and -not ($e.FullName -match '(^|/)(libraries|assets|runtime|versions)/')){
            try{
              $tmp=Join-Path $tempRoot ([Guid]::NewGuid().ToString('N') + '.jar')
              [IO.Compression.ZipFileExtensions]::ExtractToFile($e,$tmp,$true)
              AddJarItem ([IO.Path]::GetFileName($e.FullName)) ($a.FullName + '!' + $e.FullName) (Get-FileHash -LiteralPath $tmp -Algorithm SHA512).Hash
              Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
            }catch{}
          }
        }
        $zip.Dispose()
      }catch{}
    }elseif($ext -eq '.rar' -or $ext -eq '.7z'){
      $seven=Find7Zip
      if($seven){
        try{
          $out=Join-Path $tempRoot ([Guid]::NewGuid().ToString('N'))
          New-Item -ItemType Directory -Force -Path $out | Out-Null
          & $seven x '-y' ('-o'+$out) $a.FullName '*.jar' | Out-Null
          Get-ChildItem -LiteralPath $out -Filter '*.jar' -File -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object {
            if($_.FullName -notmatch '\\(libraries|assets|runtime|versions)\\'){
              try{$rel=$_.FullName.Substring($out.Length).TrimStart('\'); AddJarItem $_.Name ($a.FullName + '!' + $rel) (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA512).Hash}catch{}
            }
          }
          Remove-Item -LiteralPath $out -Recurse -Force -ErrorAction SilentlyContinue
        }catch{}
      }
    }
  }
}finally{
  Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
if($items.Count -eq 0){'mods not found'; exit}
$known=@{}
for($i=0;$i -lt $items.Count;$i+=50){
  $to=[Math]::Min($i+49,$items.Count-1)
  $batch=@($items[$i..$to])
  $body=@{hashes=@($batch|ForEach-Object{$_.Hash});algorithm='sha512'}|ConvertTo-Json -Compress
  try{
    $res=Invoke-RestMethod -Uri 'https://api.modrinth.com/v2/version_files' -Method Post -ContentType 'application/json' -Body $body -Headers @{'User-Agent'='HolyCheck/1.0'} -TimeoutSec 20
    if($res){$res.PSObject.Properties.Name|ForEach-Object{$known[$_]=1}}
  }catch{}
}
$bad=0
foreach($it in $items){
  if(-not $known.ContainsKey($it.Hash)){
    $bad++
    '{0} | {1}' -f $it.Name,$it.Path
  }
}
if($bad -eq 0){'не найдено'}
)HCPS";

    std::wstring output;
    if (!RunHiddenPowerShellScriptCapture(script, output, 300000))
    {
        lines.push_back(L"не удалось запустить checkmods");
        return lines;
    }

    std::vector<std::wstring> raw = SplitAutomationTextLines(output);
    for (const std::wstring& line : raw)
    {
        if (!line.empty())
            AppendWrappedAutomationLine(lines, line, 180);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}

static std::vector<std::wstring> ScanUnsignedDlls()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"название | путь");

    std::wstring script = LR"HCPS(
$ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
function AddRoot([string]$p){ if(-not [string]::IsNullOrWhiteSpace($p) -and (Test-Path -LiteralPath $p)){ $script:roots += (Resolve-Path -LiteralPath $p).Path } }
function IsUserDll([System.IO.FileInfo]$f){
  $p=$f.FullName
  if($p -match '^C:\\(Windows|Program Files|Program Files \(x86\)|ProgramData|Recovery|System Volume Information|\$Recycle\.Bin)(\\|$)'){return $false}
  if($p -match '\\(WindowsApps|Microsoft|Package Cache|AppData\\Local\\Packages|AppData\\Local\\Microsoft|AppData\\Local\\Programs|Python|Python\d+|pip|nodejs|gradle|maven|\.gradle|\.m2|jdk|jre|java|libraries|assets|runtime|versions|site-packages|venv|node_modules)\\'){return $false}
  if($p -match '\\(Downloads|Desktop)\\'){return $true}
  if($p -match '\\.minecraft\\'){return $true}
  if($p -match '\\.tlauncher\\legacy\\Minecraft\\game\\'){return $true}
  return $false
}
$roots=@()
if(Test-Path -LiteralPath 'C:\Users'){
  Get-ChildItem -LiteralPath 'C:\Users' -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
    AddRoot (Join-Path $_.FullName 'Downloads')
    AddRoot (Join-Path $_.FullName 'Desktop')
    AddRoot (Join-Path $_.FullName 'AppData\Roaming\.minecraft')
    AddRoot (Join-Path $_.FullName 'AppData\Roaming\.tlauncher\legacy\Minecraft\game')
  }
}
if($env:USERPROFILE){ AddRoot (Join-Path $env:USERPROFILE 'Downloads'); AddRoot (Join-Path $env:USERPROFILE 'Desktop') }
if($env:APPDATA){ AddRoot (Join-Path $env:APPDATA '.minecraft'); AddRoot (Join-Path $env:APPDATA '.tlauncher\legacy\Minecraft\game') }
$roots=@($roots | Where-Object {$_} | Select-Object -Unique)
if($roots.Count -eq 0){'minecraft folder not found'; exit}
$seen=@{}
$count=0
foreach($r in $roots){
  try{
    Get-ChildItem -LiteralPath $r -Filter '*.dll' -File -Recurse -Force -ErrorAction SilentlyContinue | Where-Object { IsUserDll $_ } | ForEach-Object {
      $key=$_.FullName.ToLowerInvariant()
      if(-not $seen.ContainsKey($key)){
        $seen[$key]=1
        try{
          $v=$_.VersionInfo
          $emptyAll=([string]::IsNullOrWhiteSpace($v.FileDescription) -and [string]::IsNullOrWhiteSpace($v.ProductName) -and [string]::IsNullOrWhiteSpace($v.LegalCopyright) -and [string]::IsNullOrWhiteSpace($v.ProductVersion))
          if($emptyAll){$count++; '{0} | {1}' -f $_.Name,$_.FullName}
        }catch{}
      }
    }
  }catch{}
}
if($count -eq 0){'не найдено'}
)HCPS";

    std::wstring output;
    if (!RunHiddenPowerShellScriptCapture(script, output, 180000))
    {
        lines.push_back(L"не удалось запустить dll scan");
        return lines;
    }

    std::vector<std::wstring> raw = SplitAutomationTextLines(output);
    for (const std::wstring& line : raw)
    {
        if (!line.empty())
            AppendWrappedAutomationLine(lines, line, 180);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}

static std::vector<std::wstring> ScanPage4ByMode(int mode)
{
    if (mode == 1)
        return ScanUnsignedDlls();
    return ScanModrinthHashes();
}

static std::vector<std::wstring> ScanInjGenStyle()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"pid | процесс | статус | детали");

    std::wstring script;
    script.reserve(32768);
    script += LR"HCPS(
$ErrorActionPreference='SilentlyContinue'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8

$src=@"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

public static class HCInjGenNative {
    const uint PROCESS_QUERY_INFORMATION = 0x0400;
    const uint PROCESS_VM_READ = 0x0010;
    const uint MEM_COMMIT = 0x1000;
    const uint PAGE_NOACCESS = 0x01;
    const uint PAGE_READONLY = 0x02;
    const uint PAGE_READWRITE = 0x04;
    const uint PAGE_WRITECOPY = 0x08;
    const uint PAGE_EXECUTE_READ = 0x20;
    const uint PAGE_EXECUTE_READWRITE = 0x40;
    const uint PAGE_EXECUTE_WRITECOPY = 0x80;
    const uint PAGE_GUARD = 0x100;

    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORY_BASIC_INFORMATION {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public UIntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }

    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)] static extern UIntPtr VirtualQueryEx(IntPtr hProcess, IntPtr address, out MEMORY_BASIC_INFORMATION mbi, UIntPtr length);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr baseAddress, byte[] buffer, IntPtr size, out IntPtr bytesRead);

    static ulong U(IntPtr p) { return unchecked((ulong)p.ToInt64()); }
    static bool Readable(uint protect) {
        if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) return false;
        uint p = protect & 0xff;
        return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    }
)HCPS";
    script += LR"HCPS(
    static bool Writable(uint protect) {
        if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) return false;
        uint p = protect & 0xff;
        return p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    }
    static bool ContainsAscii(byte[] data, int len, string needle) {
        byte[] n = Encoding.ASCII.GetBytes(needle);
        if (n.Length == 0 || len < n.Length) return false;
        for (int i = 0; i <= len - n.Length; i++) {
            int j = 0;
            for (; j < n.Length; j++) if (data[i + j] != n[j]) break;
            if (j == n.Length) return true;
        }
        return false;
    }
    static bool FindDwordInRange(IntPtr hp, ulong start, ulong end, uint value) {
        const int chunk = 1024 * 1024;
        ulong cur = start;
        int mbiSize = Marshal.SizeOf(typeof(MEMORY_BASIC_INFORMATION));
        while (cur < end) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQueryEx(hp, new IntPtr(unchecked((long)cur)), out mbi, (UIntPtr)mbiSize) == UIntPtr.Zero) { cur += 0x1000; continue; }
            ulong rb = U(mbi.BaseAddress);
            ulong rs = mbi.RegionSize.ToUInt64();
            if (rs == 0) break;
            ulong re = rb + rs;
            if (mbi.State == MEM_COMMIT && Writable(mbi.Protect)) {
                ulong a = Math.Max(cur, rb);
                ulong b = Math.Min(end, re);
                for (ulong pos = a; pos < b; ) {
                    int want = (int)Math.Min((ulong)chunk, b - pos);
                    byte[] buf = new byte[want];
                    IntPtr got;
                    if (ReadProcessMemory(hp, new IntPtr(unchecked((long)pos)), buf, new IntPtr(want), out got)) {
                        int n = (int)Math.Min((long)want, got.ToInt64());
                        for (int i = 0; i + 4 <= n; i += 4) {
                            uint v = BitConverter.ToUInt32(buf, i);
                            if (v == value) return true;
                        }
                    }
                    pos += (ulong)Math.Max(want, 0x1000);
                }
            }
            cur = re > cur ? re : cur + 0x1000;
        }
)HCPS";
    script += LR"HCPS(
        return false;
    }
    static bool FindAsciiInWritableMemory(IntPtr hp, string needle) {
        const int chunk = 1024 * 1024;
        ulong cur = 0;
        ulong max = IntPtr.Size == 8 ? 0x00007fffffffffffUL : 0x7fffffffUL;
        int mbiSize = Marshal.SizeOf(typeof(MEMORY_BASIC_INFORMATION));
        while (cur < max) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQueryEx(hp, new IntPtr(unchecked((long)cur)), out mbi, (UIntPtr)mbiSize) == UIntPtr.Zero) { cur += 0x10000; continue; }
            ulong rb = U(mbi.BaseAddress);
            ulong rs = mbi.RegionSize.ToUInt64();
            if (rs == 0) break;
            ulong re = rb + rs;
            if (mbi.State == MEM_COMMIT && Writable(mbi.Protect)) {
                for (ulong pos = rb; pos < re; ) {
                    int want = (int)Math.Min((ulong)chunk, re - pos);
                    byte[] buf = new byte[want];
                    IntPtr got;
                    if (ReadProcessMemory(hp, new IntPtr(unchecked((long)pos)), buf, new IntPtr(want), out got)) {
                        int n = (int)Math.Min((long)want, got.ToInt64());
                        if (ContainsAscii(buf, n, needle)) return true;
                    }
                    pos += (ulong)Math.Max(want, 0x1000);
                }
            }
            cur = re > cur ? re : cur + 0x10000;
        }
        return false;
    }
    static ProcessModule FindJvm(Process p) {
        try {
            foreach (ProcessModule m in p.Modules) {
                string n = (m.ModuleName ?? "").ToLowerInvariant();
                string f = (m.FileName ?? "").ToLowerInvariant();
                if (n == "jvm.dll" || f.EndsWith("\\jvm.dll")) return m;
            }
        } catch { }
        return null;
    }
    static bool IsJavaCandidate(Process p) {
        string n = (p.ProcessName ?? "").ToLowerInvariant();
        if (n == "java" || n == "javaw") return true;
        if (n.Contains("minecraft") || n.Contains("lunar") || n.Contains("feather") || n.Contains("badlion") || n.Contains("tlauncher")) return true;
        try {
)HCPS";
    script += LR"HCPS(
            string t = (p.MainWindowTitle ?? "").ToLowerInvariant();
            if (t.Contains("minecraft") || t.Contains("lunar") || t.Contains("feather") || t.Contains("badlion") || t.Contains("tlauncher")) return true;
        } catch { }
        return false;
    }
    public static string Run() {
        StringBuilder sb = new StringBuilder();
        int scanned = 0;
        foreach (Process p in Process.GetProcesses()) {
            if (!IsJavaCandidate(p)) continue;
            ProcessModule jvm = FindJvm(p);
            if (jvm == null) continue;
            scanned++;
            IntPtr hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, p.Id);
            if (hp == IntPtr.Zero) { sb.AppendLine(p.Id + " | " + p.ProcessName + " | error | no process access"); continue; }
            try {
                ulong baseAddr = U(jvm.BaseAddress);
                ulong endAddr = baseAddr + (ulong)jvm.ModuleMemorySize;
                bool sigA = FindDwordInRange(hp, baseAddr, endAddr, 524294U);
                bool sigB = FindDwordInRange(hp, baseAddr, endAddr, 4242546329U);
                if (sigA && sigB) {
                    bool lunarPattern = FindAsciiInWritableMemory(hp, "com/lunarclient");
                    if (lunarPattern) sb.AppendLine(p.Id + " | " + p.ProcessName + " | detected | Generic injection detected");
                    else sb.AppendLine(p.Id + " | " + p.ProcessName + " | detected | Injection detected in untested game client");
                } else {
                    sb.AppendLine(p.Id + " | " + p.ProcessName + " | clean | suspicious java agents not detected");
                }
            } catch (Exception ex) {
                sb.AppendLine(p.Id + " | " + p.ProcessName + " | error | " + ex.Message.Replace('\r',' ').Replace('\n',' '));
            } finally { CloseHandle(hp); }
        }
        if (scanned == 0) return "minecraft/java process not found";
        return sb.ToString();
    }
}
"@

try {
  Add-Type -TypeDefinition $src -Language CSharp -ErrorAction Stop
  [HCInjGenNative]::Run()
} catch {
  'injgen native scanner error: ' + (($_.Exception.Message) -replace '\r|\n',' ')
}
)HCPS";

    std::wstring output;
    if (!RunHiddenPowerShellScriptCapture(script, output, 180000))
    {
        lines.push_back(L"не удалось запустить injgen");
        return lines;
    }

    std::vector<std::wstring> raw = SplitAutomationTextLines(output);
    for (const std::wstring& line : raw)
    {
        if (!line.empty() && ToLowerCopy(line).find(L"filename, directory name, or volume label") == std::wstring::npos)
            AppendWrappedAutomationLine(lines, line, 180);
    }

    if (lines.size() == 1)
        lines.push_back(L"не найдено");

    return lines;
}

static std::vector<std::wstring> ExecuteAutomationScanTarget(int target, bool unsignedOnly)
{
    std::vector<std::wstring> lines;

    if (target == AUTOMATION_OUTPUT_BAM)
        lines = ScanBamRegistry(unsignedOnly);
    else if (target == AUTOMATION_OUTPUT_EVENTVWR)
        lines = ScanEventViewerLogs();
    else if (target == AUTOMATION_OUTPUT_HISTORYPW)
        lines = ScanPowerShellHistory();
    else if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        lines = ScanJarActivityJournalReasonsOnly();
    else if (target == AUTOMATION_OUTPUT_EXE_DELETED)
        // .exe deleted больше не отдельное окно: deleted-строки показываются внутри .exe activity.
        lines = ScanExeActivityWithDeleted();
    else if (target == AUTOMATION_OUTPUT_PF_DELETED)
        lines = ScanUsnActivityForExtension(L".jar", true);
    else if (target == AUTOMATION_OUTPUT_MC_LOGS)
        lines = ScanExeDeletedFromUsnDeleteClose();
    else if (target == AUTOMATION_OUTPUT_PAGE4)
        lines = ScanModrinthHashes();
    else if (target == AUTOMATION_OUTPUT_DLL_UNSIGNED)
        lines = ScanUnsignedDlls();
    else if (target == AUTOMATION_OUTPUT_INJGEN)
        lines = ScanInjGenStyle();

    if (target == AUTOMATION_OUTPUT_JAR_ACTIVITY)
        SortAutomationOutputLinesByPipeColumnDateDesc(lines, 0, 1);
    else
        SortAutomationOutputLinesByDateDesc(lines, target == AUTOMATION_OUTPUT_EVENTVWR ? 0 : 1);
    return lines;
}

static void PostAutomationBatchDone(std::vector<AutomationBatchItem>&& items)
{
    HWND hwnd = g_hMain;

    if (!hwnd || g_destroying.load())
        return;

    AutomationBatchResult* result = new (std::nothrow) AutomationBatchResult();
    if (!result)
        return;

    result->items = std::move(items);
    if (!PostMessageW(hwnd, WM_APP_AUTOMATION_BATCH_DONE, 0, reinterpret_cast<LPARAM>(result)))
        delete result;
}

static void StartAutomationScan(int target)
{
    bool expected = false;
    std::atomic_bool& working = AutomationWorkingRef(target);

    if (!working.compare_exchange_strong(expected, true))
    {
        SetAutomationStatusLine(target, L"сканирование...");
        InvalidateAutomationPanel(target);
        return;
    }

    bool unsignedOnly = g_bamUnsignedOnly.load();
    SetAutomationStatusLine(target, L"сканирование...");
    if (AutomationEditForTarget(target))
        UpdateAutomationEditWindow(target);
    InvalidateAutomationPanel(target);

    std::thread([target, unsignedOnly]()
    {
        std::vector<std::wstring> lines = ExecuteAutomationScanTarget(target, unsignedOnly);
        PostAutomationDone(target, std::move(lines));
    }).detach();
}

static void StartAutomationBatchScan(const std::vector<int>& targets)
{
    std::vector<int> started;
    started.reserve(targets.size());

    for (int target : targets)
    {
        bool expected = false;
        std::atomic_bool& working = AutomationWorkingRef(target);
        if (working.compare_exchange_strong(expected, true))
        {
            started.push_back(target);
            SetAutomationStatusLine(target, L"сканирование...");
            if (AutomationEditForTarget(target))
                UpdateAutomationEditWindow(target);
            InvalidateAutomationPanel(target);
        }
        else
        {
            SetAutomationStatusLine(target, L"сканирование...");
            InvalidateAutomationPanel(target);
        }
    }

    if (started.empty())
        return;

    bool unsignedOnly = g_bamUnsignedOnly.load();

    std::thread([started, unsignedOnly]()
    {
        std::vector<AutomationBatchItem> items;
        std::mutex itemsMutex;
        std::vector<std::thread> workers;
        workers.reserve(started.size());

        for (int target : started)
        {
            workers.emplace_back([target, unsignedOnly, &items, &itemsMutex]()
            {
                AutomationBatchItem item;
                item.target = target;
                item.lines = ExecuteAutomationScanTarget(target, unsignedOnly);
                std::lock_guard<std::mutex> lock(itemsMutex);
                items.push_back(std::move(item));
            });
        }

        for (std::thread& worker : workers)
        {
            if (worker.joinable())
                worker.join();
        }

        PostAutomationBatchDone(std::move(items));
    }).detach();
}


static bool IsAllowedDownloadLogLine(const std::wstring& line)
{
    return line.rfind(L"скачивается:", 0) == 0 ||
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
    const std::wstring p1 = L"скачивается: " + name;
    const std::wstring p2 = L"скачивается: " + name;

    return line.rfind(p1, 0) == 0 || line.rfind(p2, 0) == 0;
}

static void UpsertLiveDownloadLogLine(const std::wstring& name, unsigned long long speedBps)
{
    if (name.empty())
        return;

    std::wstring line = L"скачивается: " + name;

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
    UNREFERENCED_PARAMETER(name);
    return false;
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
    UNREFERENCED_PARAMETER(item);
    return false;
}

static DWORD GetDirectDownloadTimeoutMs(const DownloadItem& item)
{
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
    UNREFERENCED_PARAMETER(attempt);
    return 0;
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

