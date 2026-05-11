// 04_zip_process_utils.inc
// ZIP extraction, quoting, PowerShell base64 helpers, forward declarations.
// Original lines: 1752-2022

static bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir)
{
    std::lock_guard<std::mutex> lock(g_zipMutex);

    if (!FileExists(zipPath))
        return false;

    if (!EnsureDirectory(destDir))
        return false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    IShellDispatch* shell = nullptr;
    hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell));

    if (FAILED(hr) || !shell)
    {
        if (needUninit)
            CoUninitialize();

        return false;
    }

    VARIANT vZip;
    VARIANT vDest;

    VariantInit(&vZip);
    VariantInit(&vDest);

    vZip.vt = VT_BSTR;
    vZip.bstrVal = SysAllocString(zipPath.c_str());

    vDest.vt = VT_BSTR;
    vDest.bstrVal = SysAllocString(destDir.c_str());

    if (!vZip.bstrVal || !vDest.bstrVal)
    {
        VariantClear(&vZip);
        VariantClear(&vDest);
        shell->Release();

        if (needUninit)
            CoUninitialize();

        return false;
    }

    Folder* zipFolder = nullptr;
    Folder* destFolder = nullptr;

    shell->NameSpace(vZip, &zipFolder);
    shell->NameSpace(vDest, &destFolder);

    if (!zipFolder || !destFolder)
    {
        if (zipFolder) zipFolder->Release();
        if (destFolder) destFolder->Release();

        VariantClear(&vZip);
        VariantClear(&vDest);
        shell->Release();

        if (needUninit)
            CoUninitialize();

        return false;
    }

    FolderItems* items = nullptr;
    zipFolder->Items(&items);

    if (!items)
    {
        destFolder->Release();
        zipFolder->Release();
        VariantClear(&vZip);
        VariantClear(&vDest);
        shell->Release();

        if (needUninit)
            CoUninitialize();

        return false;
    }

    long itemCount = 0;
    items->get_Count(&itemCount);

    VARIANT vItems;
    VARIANT vOptions;

    VariantInit(&vItems);
    VariantInit(&vOptions);

    vItems.vt = VT_DISPATCH;
    vItems.pdispVal = items;

    // 4 = no progress UI, 16 = yes to all, 512 = do not confirm mkdir,
    // 1024 = do not show error UI.
    vOptions.vt = VT_I4;
    vOptions.lVal = 4 | 16 | 512 | 1024;

    hr = destFolder->CopyHere(vItems, vOptions);

    ULONGLONG started = GetTickCount64();
    bool copiedSomething = itemCount <= 0;

    while (SUCCEEDED(hr) && !IsCancelRequested() && GetTickCount64() - started < ZIP_EXTRACT_TIMEOUT_MS)
    {
        std::wstring exePath;

        if (FindFirstExeRecursive(destDir, exePath))
        {
            copiedSomething = true;
            break;
        }

        WIN32_FIND_DATAW data{};
        std::wstring mask = CombinePath(destDir, L"*");
        HANDLE hFind = FindFirstFileW(mask.c_str(), &data);

        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
                {
                    copiedSomething = true;
                    break;
                }
            }
            while (FindNextFileW(hFind, &data));

            FindClose(hFind);
        }

        if (copiedSomething && GetTickCount64() - started >= 1500)
            break;

        Sleep(ZIP_EXTRACT_POLL_MS);
    }

    items->Release();
    destFolder->Release();
    zipFolder->Release();

    VariantClear(&vZip);
    VariantClear(&vDest);
    shell->Release();

    if (needUninit)
        CoUninitialize();

    return SUCCEEDED(hr) && copiedSomething && !IsCancelRequested();
}

static std::wstring EscapePowerShellSingleQuoted(std::wstring s)
{
    size_t pos = 0;

    while ((pos = s.find(L'\'', pos)) != std::wstring::npos)
    {
        s.insert(pos, L"'");
        pos += 2;
    }

    return s;
}
static std::wstring QuoteProcessArgument(const std::wstring& arg)
{
    if (arg.empty())
        return L"\"\"";

    bool needQuotes = false;

    for (wchar_t ch : arg)
    {
        if (iswspace(ch) || ch == L'\"')
        {
            needQuotes = true;
            break;
        }
    }

    if (!needQuotes)
        return arg;

    std::wstring out = L"\"";
    size_t backslashes = 0;

    for (wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++backslashes;
        }
        else if (ch == L'\"')
        {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
        }
        else
        {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }

    out.append(backslashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static std::wstring GetSystemToolPath(const wchar_t* relativePath)
{
    wchar_t systemDir[MAX_PATH * 2]{};
    UINT len = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));

    if (len == 0 || len >= ARRAYSIZE(systemDir))
        return relativePath ? relativePath : L"";

    std::wstring path = CombinePath(systemDir, relativePath ? relativePath : L"");
    return FileExists(path) ? path : std::wstring(relativePath ? relativePath : L"");
}

static std::wstring Base64EncodeBytes(const unsigned char* data, size_t len)
{
    static const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int value = data[i] << 16;
        bool has2 = i + 1 < len;
        bool has3 = i + 2 < len;

        if (has2)
            value |= data[i + 1] << 8;

        if (has3)
            value |= data[i + 2];

        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(has2 ? alphabet[(value >> 6) & 0x3F] : L'=');
        out.push_back(has3 ? alphabet[value & 0x3F] : L'=');
    }

    return out;
}

static std::wstring Base64EncodePowerShellCommand(const std::wstring& command)
{
    return Base64EncodeBytes(reinterpret_cast<const unsigned char*>(command.data()), command.size() * sizeof(wchar_t));
}



static bool DownloadWithCurlFallback(const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs = 18000);
static bool DownloadWithPowerShellFallback(const std::wstring& url, const std::wstring& targetPath, DWORD timeoutMs = 22000);
static bool DownloadRecursiveValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath);
static bool DownloadCurlValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath);
static bool DownloadPowerShellValidated(const DownloadItem& item, const std::wstring& url, const std::wstring& targetPath);

