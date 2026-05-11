// 01_prelude_globals.inc
// Заголовки, defines, структуры, глобальные переменные, список DownloadItem.
// Original lines: 1-386

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <ole2.h>
#include <shldisp.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <tlhelp32.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <regex>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cctype>
#include <iterator>
#include <memory>
#include <initializer_list>
#include <new>
#include <climits>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "advapi32.lib")

using namespace Gdiplus;

#define IDC_TAB_BASE        2000
#define IDC_DOWNLOAD_ALL    2100
#define IDC_POWERSHELL      2104
#define IDC_ENABLE_SERVICES 2105
#define IDC_DELETE_HOLYCHECK 2106
#define IDC_CANCEL_DOWNLOAD 2107
#define IDC_COPY_EVERYTHING_1 2108
#define IDC_COPY_EVERYTHING_2 2109
#define IDC_COPY_POWERSHELL 2110
#define IDC_COPY_SITES      2111
#define IDC_TILE_BASE       3000

#define TAB_COUNT           4

#define WM_APP_PROGRESS     (WM_APP + 2)
#define WM_APP_DONE         (WM_APP + 3)
#define WM_APP_LOG          (WM_APP + 4)
#define WM_APP_ITEM_DONE    (WM_APP + 5)
#define WM_APP_REFRESH      (WM_APP + 6)

#define TIMER_PARTICLES     9001

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 0x00000080
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

struct DownloadItem
{
    std::wstring name;
    std::wstring url;
    std::wstring file;
    int tab;
};

struct ParsedUrl
{
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring pathAndQuery;
    bool https = false;
};


enum class DownloadError
{
    None,
    Cancelled,
    InvalidUrl,
    OpenSessionFailed,
    ConnectFailed,
    OpenRequestFailed,
    SendFailed,
    ReceiveFailed,
    HttpStatus,
    HtmlInsteadOfFile,
    HtmlLinkNotFound,
    ReadFailed,
    FileOpenFailed,
    FileWriteFailed,
    MoveFailed,
    SizeMismatch,
    TooManyRedirects
};

struct DownloadResult
{
    bool ok = false;
    DownloadError error = DownloadError::None;
    DWORD httpStatus = 0;
    DWORD win32Error = 0;
    unsigned long long contentLength = 0;
    unsigned long long bytesWritten = 0;
    unsigned long long resumeFrom = 0;
    bool resumed = false;
    bool retryable = false;
    std::wstring finalUrl;
    std::wstring message;
};

static DownloadResult MakeDownloadError(DownloadError error, DWORD win32Error = 0, bool retryable = true)
{
    DownloadResult result;
    result.ok = false;
    result.error = error;
    result.win32Error = win32Error;
    result.retryable = retryable;
    return result;
}

static DownloadResult MakeDownloadOk()
{
    DownloadResult result;
    result.ok = true;
    result.error = DownloadError::None;
    return result;
}

struct Particle
{
    float x;
    float y;
    float vx;
    float vy;
    float size;
    BYTE alpha;
};

HINSTANCE g_hInst = nullptr;
HWND g_hMain = nullptr;
HWND g_hDownloadAll = nullptr;
HWND g_hPowerShell = nullptr;
HWND g_hEnableServices = nullptr;
HWND g_hDeleteHolyCheck = nullptr;
HWND g_hCancelDownload = nullptr;
HWND g_hCopyEverything1 = nullptr;
HWND g_hCopyEverything2 = nullptr;
HWND g_hCopyPowerShell = nullptr;
HWND g_hCopySites = nullptr;

HFONT g_hFont = nullptr;
HFONT g_hSmallFont = nullptr;
HFONT g_hTinyFont = nullptr;

std::vector<HWND> g_tabButtons;
std::vector<HWND> g_tileButtons;
std::vector<Particle> g_particles;

std::atomic_bool g_working(false);
std::atomic_bool g_cancelRequested(false);
std::atomic_bool g_deleteInProgress(false);
std::atomic_bool g_destroying(false);
std::atomic_int g_activeSingleDownloads(0);
std::atomic_int g_singleTotal(0);
std::atomic_int g_singleDone(0);
std::mutex g_zipMutex;
std::mutex g_logMutex;
std::mutex g_activeDownloadsMutex;
std::mutex g_itemBusyMutex;
std::mutex g_httpHandlesMutex;
std::mutex g_ericDownloadMutex;

struct HttpHandleSlot
{
    HINTERNET handle = nullptr;
    bool closed = false;
};

std::vector<std::shared_ptr<HttpHandleSlot>> g_httpHandles;
std::vector<BYTE> g_itemBusy;
std::vector<std::wstring> g_logLines;
std::vector<std::wstring> g_activeDownloads;
int g_logScrollOffset = 0;

int g_currentTab = 0;
int g_progressPercent = 0;
ULONG_PTR g_gdiplusToken = 0;
bool g_particlesPaused = false;
DWORD g_uiThreadId = 0;

static const size_t HTML_READ_LIMIT = 4 * 1024 * 1024;
static const DWORD ZIP_EXTRACT_TIMEOUT_MS = 120000;
static const DWORD ZIP_EXTRACT_POLL_MS = 250;

const COLORREF BG = RGB(18, 18, 19);
const COLORREF PANEL = RGB(40, 40, 44);
const COLORREF PANEL_HOVER = RGB(56, 56, 62);
const COLORREF PANEL_PRESSED = RGB(70, 70, 76);
const COLORREF BORDER = RGB(72, 72, 80);
const COLORREF TEXT = RGB(242, 242, 245);
const COLORREF BLUE_LIGHT = RGB(115, 165, 255);

static const int LOG_PANEL_X = 28;
static const int LOG_PANEL_W = 438;
static const int LOG_PANEL_H = 86;
static const int LOG_PANEL_BOTTOM_MARGIN = 18;
static const int LOG_INVALIDATE_HEIGHT = 126;
static const size_t LOG_MAX_LINES = 200;
static const size_t LOG_VISIBLE_LINES = 4;
static const int CANCEL_BUTTON_W = 84;
static const int CANCEL_BUTTON_H = 27;
static const int HELPER_PANEL_X = 28;
static const int HELPER_PANEL_W = 418;
static const int HELPER_PANEL_H = 146;
static const int COPY_BUTTON_W = 82;
static const int COPY_BUTTON_H = 22;

static RECT GetDownloadLogRect(const RECT& rc)
{
    RECT out{};
    out.left = LOG_PANEL_X;
    out.right = LOG_PANEL_X + LOG_PANEL_W;
    out.top = rc.bottom - LOG_PANEL_H - LOG_PANEL_BOTTOM_MARGIN;
    out.bottom = out.top + LOG_PANEL_H;

    if (out.top < 76)
    {
        out.top = 76;
        out.bottom = out.top + LOG_PANEL_H;
    }

    return out;
}

static RECT GetHelperTextRect(const RECT& rc)
{
    RECT logRect = GetDownloadLogRect(rc);
    RECT out{};
    out.left = HELPER_PANEL_X;
    out.right = HELPER_PANEL_X + HELPER_PANEL_W;
    out.top = logRect.top - HELPER_PANEL_H - 16;

    if (out.top < 382)
        out.top = 382;

    out.bottom = out.top + HELPER_PANEL_H;

    if (out.bottom > logRect.top - 10)
    {
        out.bottom = logRect.top - 10;
        out.top = out.bottom - HELPER_PANEL_H;
    }

    return out;
}

static bool PointInRect(const RECT& rc, POINT pt)
{
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

static int GetMaxLogScrollOffsetLocked()
{
    if (g_logLines.size() <= LOG_VISIBLE_LINES)
        return 0;

    return static_cast<int>(g_logLines.size() - LOG_VISIBLE_LINES);
}

static void ClampLogScrollOffsetLocked()
{
    int maxOffset = GetMaxLogScrollOffsetLocked();

    if (g_logScrollOffset < 0)
        g_logScrollOffset = 0;

    if (g_logScrollOffset > maxOffset)
        g_logScrollOffset = maxOffset;
}

static bool ScrollDownloadLog(int wheelDelta)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    int maxOffset = GetMaxLogScrollOffsetLocked();

    if (maxOffset <= 0)
        return false;

    int oldOffset = g_logScrollOffset;
    int step = std::max(1, std::abs(wheelDelta) / WHEEL_DELTA);

    if (wheelDelta > 0)
        g_logScrollOffset += step;
    else if (wheelDelta < 0)
        g_logScrollOffset -= step;

    ClampLogScrollOffsetLocked();
    return g_logScrollOffset != oldOffset;
}

static std::vector<DownloadItem> g_items =
{
    { L"Everything",           L"https://www.voidtools.com/Everything-1.5.0.1404a.x64.zip", L"Everything-1.5.0.1404a.x64.zip", 0 },
    { L"ShellBag Analyzer",    L"https://privazer.com/ru/shellbag_analyzer_cleaner.exe", L"shellbag_analyzer_cleaner.exe", 0 },
    { L"RecentFilesView",      L"https://www.nirsoft.net/utils/recentfilesview.zip", L"recentfilesview.zip", 0 },
    { L"SystemInformer",       L"https://downloads.sourceforge.net/project/systeminformer/systeminformer-3.2.25011-release-setup.exe", L"systeminformer-3.2.25011-release-setup.exe", 0 },
    { L"LastActivityView",     L"https://www.nirsoft.net/utils/lastactivityview.zip", L"lastactivityview.zip", 0 },
    { L"ExecutedProgramsList", L"https://www.nirsoft.net/utils/executedprogramslist.zip", L"executedprogramslist.zip", 0 },
    { L"RegScanner",           L"https://www.nirsoft.net/utils/regscanner.zip", L"regscanner.zip", 0 },
    { L"USBDriveLog",          L"https://www.nirsoft.net/utils/usbdrivelog.zip", L"usbdrivelog.zip", 0 },
    { L"VersionChecker",       L"https://github.com/HolyWorldWEB/VersionChecker/releases/download/v1.0.0/MinecraftVersionChecker.exe", L"MinecraftVersionChecker.exe", 0 },
    { L"ProcessHacker",        L"https://github.com/processhacker/processhacker/releases/download/v2.39/processhacker-2.39-setup.exe", L"processhacker-2.39-setup.exe", 0 },
    { L"USBDeview",            L"https://www.nirsoft.net/utils/usbdeview-x64.zip", L"usbdeview-x64.zip", 0 },
    { L"BrowserDownloadsView", L"https://www.nirsoft.net/utils/browserdownloadsview.zip", L"browserdownloadsview.zip", 0 },
    { L"JournalTrace",         L"https://github.com/spokwn/JournalTrace/releases/download/1.2/JournalTrace.exe", L"JournalTrace.exe", 0 },
    { L"WinRAR",               L"https://www.win-rar.com/postdownload.html?&L=4", L"winrar-x64.exe", 0 },

    { L"BAMParser",            L"https://github.com/spokwn/BAM-parser/releases/download/v1.2.8/BAMParser.exe", L"BAMParser.exe", 1 },
    { L"RegistryExplorer",     L"https://github.com/wearest/holycheker/releases/download/wearest/RegistryExplorer.zip", L"RegistryExplorer.zip", 1 },
    { L"JumpListExplorer",     L"https://github.com/wearest/holycheker/releases/download/wearest/JumpListExplorer.zip", L"JumpListExplorer.zip", 1 },
    { L"SimpleUnlocker",       L"https://github.com/wearest/holycheker/releases/download/wearest/simpleunlocker_release.zip", L"simpleunlocker_release.zip", 1 },
    { L"WinPrefetchView",      L"https://www.nirsoft.net/utils/winprefetchview.zip", L"winprefetchview.zip", 1 },
    { L"Recuva",               L"https://www.softportal.com/getsoft-5667-recuva-3.html", L"rcsetup-en.exe", 1 },

    { L"PathsParser",          L"https://github.com/spokwn/PathsParser/releases/download/v1.1/PathsParser.exe", L"PathsParser.exe", 2 },
    { L"Detect It Easy",       L"https://github.com/horsicq/Detect-It-Easy/releases/download/Beta/die_win64_portable_3.10_x64.zip", L"die_win64_portable_3.10_x64.zip", 2 },
    { L"Recaf CLI",            L"https://github.com/Col-E/Recaf-Launcher/releases/download/0.8.8/recaf-cli-0.8.8.jar", L"recaf-cli-0.8.8.jar", 2 },

    { L"BrowsingHistoryView",  L"https://www.nirsoft.net/utils/browsinghistoryview.zip", L"browsinghistoryview.zip", 3 },
    { L"AlternateStreamView",  L"https://www.nirsoft.net/utils/alternatestreamview-x64.zip", L"alternatestreamview-x64.zip", 3 },
    { L"WinDefLogView",        L"https://www.nirsoft.net/utils/windeflogview.zip", L"windeflogview.zip", 3 },
    { L"VolatilityWorkbench",  L"https://www.osforensics.com/downloads/VolatilityWorkbench.zip", L"VolatilityWorkbench.zip", 3 },
    { L"KernelLiveDumpTool",   L"https://github.com/spokwn/KernelLiveDumpTool/releases/download/v1.1/KernelLiveDumpTool.exe", L"KernelLiveDumpTool.exe", 3 }
};

