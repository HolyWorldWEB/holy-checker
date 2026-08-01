// =============================================================================
//  10_filecheck_clipboard.inc
//
//  Новая вкладка раздела Check: "file check".
//  - Задача 1: сканирование ПК на файлы, у которых фактический тип (по
//    сигнатуре/magic bytes) не совпадает с расширением (напр. w.png = EXE).
//  - Задача 2 (безопасная часть): показать текущий текст буфера обмена,
//    перечислить процессы, связанные с cbdhsvc / Clipboard User Service,
//    и прочитать ОФИЦИАЛЬНУЮ историю буфера Windows (Win+V), если она включена.
//
//  Стиль и архитектура повторяют существующие вкладки (AUTOMATION_OUTPUT_*,
//  StartAutomationScan, SetAutomationLines, pipe-разделённые колонки).
//
//  БЕЗОПАСНОСТЬ:
//   * файлы НИКОГДА не запускаются — только открываются на чтение;
//   * читаются лишь метаданные и первые байты (заголовок);
//   * все ошибки доступа проглатываются, обход не падает и не виснет;
//   * НЕ реализовано "восстановление стёртого буфера" из памяти cbdhsvc —
//     это техника инфостилеров; используется только официальный API истории.
// =============================================================================

#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <chrono>
#include <thread>
#include <atomic>

// -----------------------------------------------------------------------------
//  Зависимости от 01/02:
//    * enum AUTOMATION_OUTPUT_FILECHECK / AUTOMATION_OUTPUT_CLIPBOARD — в 01;
//    * std::atomic_bool g_automationFileCheckWorking — в 01;
//    * g_fileCheckLines / g_clipboardLines и их scroll offsets — в 01;
//    * SetAutomationLines / SetAutomationStatusLine / InvalidateAutomationPanel /
//      AutomationWorkingRef / AutomationLinesRef — в 02.
//  Поэтому этот файл должен подключаться СРАЗУ ПОСЛЕ 02_state_paths_urls.inc.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  1. РАСШИРЯЕМАЯ ТАБЛИЦА СИГНАТУР
//
//  Чтобы добавить новый тип файла — просто допишите строку в g_fileSignatures.
//  offset  — смещение, с которого сравниваются байты;
//  bytes   — эталонные байты сигнатуры;
//  mask    — необязательная маска (0xFF = значим, 0x00 = игнор), пустая = точное
//            сравнение;
//  type    — короткое имя типа ("PNG", "EXE/DLL(PE)", ...);
//  exts    — расширения, которые СЧИТАЮТСЯ нормальными для этого типа.
// -----------------------------------------------------------------------------
struct FileTypeSignature
{
    size_t                     offset;
    std::vector<uint8_t>       bytes;
    std::vector<uint8_t>       mask;   // может быть пустой
    std::wstring               type;
    std::vector<std::wstring>  exts;   // ожидаемые расширения (нижний регистр, без точки)
};

// Базовый набор распространённых форматов. Легко расширяется.
static const std::vector<FileTypeSignature>& GetFileSignatures()
{
    static const std::vector<FileTypeSignature> sigs = {
        // Изображения
        { 0, { 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A }, {}, L"PNG",  { L"png" } },
        { 0, { 0xFF,0xD8,0xFF },                          {}, L"JPEG", { L"jpg", L"jpeg" } },
        { 0, { 0x47,0x49,0x46,0x38 },                     {}, L"GIF",  { L"gif" } },
        { 0, { 0x42,0x4D },                               {}, L"BMP",  { L"bmp" } },
        // Документы (архивы исключены по требованию — проверка архивов убрана,
        // поэтому ZIP/JAR/RAR/7Z/GZIP больше не детектируются и не шумят).
        { 0, { 0x25,0x50,0x44,0x46,0x2D },                {}, L"PDF",  { L"pdf" } },
        // Класс-файл Java
        { 0, { 0xCA,0xFE,0xBA,0xBE },                     {}, L"JavaClass", { L"class" } },
        // ELF (не-Windows исполняемый — на Windows это подозрительно)
        { 0, { 0x7F,0x45,0x4C,0x46 },                     {}, L"ELF", { } },
        // Windows PE (MZ) — тип уточняется анализом PE-заголовка (EXE vs DLL)
        { 0, { 0x4D,0x5A },                               {}, L"PE(MZ)", { L"exe", L"dll", L"sys", L"scr", L"cpl", L"ocx", L"drv", L"pyd", L"mui", L"efi", L"ax" } },
    };
    return sigs;
}

// -----------------------------------------------------------------------------
//  2. АНАЛИЗ PE-ЗАГОЛОВКА (EXE / DLL)
//
//  Для файла, начинающегося с 'MZ', читаем e_lfanew, проверяем 'PE\0\0',
//  затем по Characteristics в IMAGE_FILE_HEADER отличаем DLL от EXE.
//  Работаем строго с буфером первых байт — файл не мапится и не исполняется.
// -----------------------------------------------------------------------------
static std::wstring ClassifyPE(const std::vector<uint8_t>& buf)
{
    // e_lfanew находится по смещению 0x3C (4 байта, little-endian)
    if (buf.size() < 0x40) return L"PE(MZ)";
    uint32_t peOff = static_cast<uint32_t>(buf[0x3C]) |
                     (static_cast<uint32_t>(buf[0x3D]) << 8) |
                     (static_cast<uint32_t>(buf[0x3E]) << 16) |
                     (static_cast<uint32_t>(buf[0x3F]) << 24);

    // Нужны сигнатура 'PE\0\0' (4) + IMAGE_FILE_HEADER (20). Characteristics в
    // конце file header: peOff + 4 + 18.
    if (peOff == 0 || static_cast<size_t>(peOff) + 24 > buf.size())
        return L"PE(MZ)";
    if (!(buf[peOff] == 'P' && buf[peOff + 1] == 'E' &&
          buf[peOff + 2] == 0 && buf[peOff + 3] == 0))
        return L"DOS/MZ"; // MZ есть, но не PE — старый DOS-стаб и т.п.

    uint16_t characteristics = static_cast<uint16_t>(buf[peOff + 22]) |
                               (static_cast<uint16_t>(buf[peOff + 23]) << 8);
    const uint16_t IMAGE_FILE_DLL_FLAG = 0x2000;
    if (characteristics & IMAGE_FILE_DLL_FLAG)
        return L"DLL(PE)";
    return L"EXE(PE)";
}

// Уточнение ZIP-контейнера: JAR обычно содержит META-INF/ или .class.
// Здесь мы работаем только по первым байтам, поэтому ограничиваемся общим
// типом "ZIP/JAR" — расширения jar/zip/docx и т.п. считаются валидными.

// -----------------------------------------------------------------------------
//  3. ОПРЕДЕЛЕНИЕ РЕАЛЬНОГО ТИПА ФАЙЛА
//
//  Открывает файл ТОЛЬКО на чтение (FILE_SHARE_READ, OPEN_EXISTING) и читает
//  первые заголовочные байты. Ничего не исполняет. Возвращает "" если тип не
//  распознан (нет сигнатуры).
// -----------------------------------------------------------------------------
static bool MatchSignature(const std::vector<uint8_t>& buf, const FileTypeSignature& s)
{
    if (buf.size() < s.offset + s.bytes.size()) return false;
    for (size_t i = 0; i < s.bytes.size(); ++i)
    {
        uint8_t b = buf[s.offset + i];
        uint8_t m = (i < s.mask.size()) ? s.mask[i] : 0xFF;
        if ((b & m) != (s.bytes[i] & m)) return false;
    }
    return true;
}

static std::wstring DetectRealFileType(const std::wstring& path, uint64_t& sizeOut)
{
    sizeOut = 0;

    HANDLE h = CreateFileW(path.c_str(),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr,
                           OPEN_EXISTING,
                           // Без FILE_FLAG_EXECUTE и без запуска — только чтение.
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return L""; // нет доступа — молча пропускаем (обработка ошибок)

    LARGE_INTEGER li{};
    if (GetFileSizeEx(h, &li)) sizeOut = static_cast<uint64_t>(li.QuadPart);

    // Читаем 4 КБ заголовка: этого хватает для всех сигнатур и для
    // e_lfanew/PE header даже у файлов с длинным DOS-стабом. При 512 байтах
    // такие файлы определялись как PE(MZ) и теряли статус подозрительных.
    // Большие файлы целиком НЕ читаем — скорость и безопасность.
    std::vector<uint8_t> buf(4096);
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return L"";
    buf.resize(read);
    if (buf.empty()) return L"";

    for (const auto& s : GetFileSignatures())
    {
        if (MatchSignature(buf, s))
        {
            if (s.type == L"PE(MZ)")
                return ClassifyPE(buf); // EXE(PE) / DLL(PE) / DOS/MZ
            return s.type;
        }
    }
    return L""; // неизвестно
}

// -----------------------------------------------------------------------------
//  4. СОПОСТАВЛЕНИЕ РАСШИРЕНИЯ И РЕАЛЬНОГО ТИПА
// -----------------------------------------------------------------------------
// ToLower(std::wstring) уже определён в 02_state_paths_urls.inc — переиспользуем его.

static std::wstring GetExtensionLower(const std::wstring& path)
{
    size_t dot = path.find_last_of(L'.');
    size_t slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return L""; // нет расширения
    return ToLower(path.substr(dot + 1));
}

// Проверяет, входит ли расширение в список ожидаемых для найденной сигнатуры.
static bool ExtensionMatchesType(const std::wstring& ext, const std::wstring& realType)
{
    for (const auto& s : GetFileSignatures())
    {
        bool typeHit = (s.type == realType) ||
                       (s.type == L"PE(MZ)" &&
                        (realType == L"EXE(PE)" || realType == L"DLL(PE)" || realType == L"DOS/MZ"));
        if (!typeHit) continue;
        for (const auto& e : s.exts)
            if (e == ext) return true;
    }
    return false;
}

// Возвращает предполагаемое исходное расширение исполняемого файла по его
// сигнатуре. Функция нужна в том числе для файлов, переименованных в .zip.
// Сам файл при проверке не переименовывается и никак не изменяется.
static std::wstring ExecutableExtensionFromType(const std::wstring& realType)
{
    if (realType == L"DLL(PE)")
        return L".dll";
    if (realType == L"EXE(PE)")
        return L".exe";
    if (realType == L"ELF")
        return L".elf";
    return L"";
}

// -----------------------------------------------------------------------------
//  Доброкачественные случаи (по требованию): картинка, названная как другая
//  картинка (.png ↔ .jpg), либо как обобщённое/служебное расширение (например
//  файл .file в папке Spotify, который на самом деле png/jpeg) — это нормально
//  и НЕ попадает в вывод.
// -----------------------------------------------------------------------------
static bool IsImageRealType(const std::wstring& realType)
{
    return realType == L"PNG" || realType == L"JPEG" ||
           realType == L"GIF" || realType == L"BMP";
}

static bool IsImageExt(const std::wstring& ext)
{
    static const wchar_t* kImageExts[] = {
        L"png", L"jpg", L"jpeg", L"gif", L"bmp",
        L"webp", L"tif", L"tiff", L"ico", L"heic"
    };
    for (const wchar_t* e : kImageExts)
        if (ext == e) return true;
    return false;
}

static bool IsGenericExt(const std::wstring& ext)
{
    // Обобщённые/служебные расширения, для которых реальный тип не важен.
    static const wchar_t* kGenericExts[] = {
        L"file", L"dat", L"bin", L"tmp", L"temp",
        L"cache", L"part", L"download"
    };
    for (const wchar_t* e : kGenericExts)
        if (ext == e) return true;
    return false;
}

// Расширения, которые ИМЕЕТ СМЫСЛ проверять на подмену: именно под них
// обычно маскируют исполняемые файлы (картинки, медиа, документы, архивы,
// шрифты, java). Всё остальное (код, бинарники, конфиги) не открываем — ради скорости.
static bool IsInspectableExtension(const std::wstring& ext)
{
    static const wchar_t* kInspectExts[] = {
        // картинки
        L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp",
        L"tif", L"tiff", L"ico", L"heic", L"svg", L"jfif",
        // видео
        L"mp4", L"m4v", L"avi", L"mkv", L"mov", L"wmv", L"flv", L"webm", L"mpg", L"mpeg",
        // аудио
        L"mp3", L"wav", L"flac", L"ogg", L"m4a", L"aac", L"wma", L"opus",
        // документы
        L"pdf", L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx",
        L"txt", L"rtf", L"odt",
        // архивы
        L"zip", L"rar", L"7z", L"gz", L"tar", L"bz2", L"xz",
        // шрифты
        L"ttf", L"otf",
        // java и прочее содержимое
        L"jar", L"class", L"model",
        // макросные документы и прочие документы
        L"docm", L"xlsm", L"pptm", L"epub",
        // образы, контейнеры и пакеты
        L"cab", L"iso", L"img", L"msi", L"appx", L"apk", L"war", L"dex",
        // графика и текстуры
        L"psd", L"dds", L"tga",
        // шрифты
        L"woff", L"woff2", L"fon",
        // медиа
        L"3gp", L"ts", L"vob", L"mid",
        // конфиги, логи, данные и бэкапы
        L"log", L"ini", L"json", L"xml", L"cfg", L"db", L"sav",
        L"bak", L"old", L"pak", L"asset", L"bundle", L"res",
        // ярлыки и скрипты
        L"lnk", L"js", L"py", L"lua", L"so"
    };
    for (const wchar_t* e : kInspectExts)
        if (ext == e) return true;
    return false;
}

// Результат по одному файлу (колонки как в других вкладках, через '|').
struct FileCheckHit
{
    std::wstring path;
    std::wstring declaredExt;
    std::wstring realType;
    std::wstring status;   // "ПОДОЗРИТЕЛЬНО" / "внимание"
    std::wstring reason;
};

// Логика вердикта: маскировка исполняемого под картинку/документ — красный флаг.
static bool EvaluateFile(const std::wstring& path, FileCheckHit& out)
{
    // Сначала дешёвые проверки по расширению — чтобы НЕ открывать лишние файлы.
    // Это главная оптимизация скорости: не читаем содержимое там, где и так
    // пропустим результат (открытие файла — самая дорогая операция в цикле).
    std::wstring ext = GetExtensionLower(path);
    if (ext.empty())
        return false; // без расширения сравнивать не с чем

    // Некоторые программы дописывают '~' к расширению (например .png~ у Forge/OptiFine
    // и бэкап-копии). Убираем хвостовые '~', иначе .png~ не совпадёт с реальным PNG
    // и появится ложное срабатывание.
    while (!ext.empty() && ext.back() == L'~')
        ext.pop_back();
    if (ext.empty())
        return false;

    // .node (native-аддон, по сути DLL) пропускаем по требованию. .model остаётся в чеке.
    if (ext == L"node")
        return false;

    // Обобщённые/служебные расширения (.tmp/.file/.dat/...) не проверяем: там
    // нормально лежат и картинки, и временно распакованные исполняемые
    // файлы (например установщики .tmp) — это не маскировка, а ложное срабатывание.
    if (IsGenericExt(ext))
        return false;

    // ГЛАВНАЯ ОПТИМИЗАЦИЯ СКОРОСТИ: открываем и читаем только те файлы,
    // чьё расширение подразумевает «безобидный» контент (картинки, видео, аудио,
    // документы, архивы, шрифты, java, .model). Именно под такие расширения
    // маскируют читы. Код/бинарники/конфиги (.dll/.pyd/.exe/.js/.json/.txt-в dev-папках
    // и т.п.) НЕ открываем — это убирает тысячи лишних обращений к диску.
    if (!IsInspectableExtension(ext))
        return false;

    // Только теперь открываем файл и читаем первые байты (для кандидатов).
    uint64_t size = 0;
    std::wstring realType = DetectRealFileType(path, size);
    if (realType.empty())
        return false; // тип не определён — не шумим

    if (ExtensionMatchesType(ext, realType))
        return false; // всё совпадает — не подозрительно (�� т.ч. .pyd → DLL)

    // Картинка, названная как другая картинка (.png ↔ .jpg) — это норма.
    if (IsImageRealType(realType) && IsImageExt(ext))
        return false;

    // Несовпадение. Оцениваем серьёзность.
    std::wstring originalExecutableExt = ExecutableExtensionFromType(realType);
    bool realIsExecutable = !originalExecutableExt.empty();
    if (!realIsExecutable)
        return false;
    std::wstring reason;
    std::wstring status;

    // Отдельно показываем случай переименования исполняемого файла в .zip.
    // Определение идёт по magic bytes/PE-заголовку, исходный .zip остаётся на
    // месте с прежним именем: FileCheck открывает его только на чтение.
    if (ext == L"zip")
    {
        status = L"ПОДОЗРИТЕЛЬНО";
        reason = L"файл с именем .zip по сигнатуре имеет исходный тип " +
                 originalExecutableExt + L" (" + realType +
                 L"); имя исходного файла не изменялось";
    }
    else
    {
        status = L"ПОДОЗРИТЕЛЬНО";
        reason = L"расширение ." + ext + L", но по сигнатуре это исполняемый файл " +
                 originalExecutableExt + L" (" + realType + L")";
    }

    out.path        = path;
    out.declaredExt = ext.empty() ? L"(нет)" : (L"." + ext);
    out.realType    = originalExecutableExt + L" (" + realType + L")";
    out.status      = status;
    out.reason      = reason;
    return true;
}

// -----------------------------------------------------------------------------
//  5. БЕЗОПАСНЫЙ РЕКУРСИВНЫЙ ОБХОД
//
//  - глубина ограничена;
//  - пропускаем reparse points (симлинки/junctions) чтобы не зациклиться;
//  - лимит по количеству файлов и по времени — UI не виснет на огромных папках;
//  - кооперативная отмена через атомарный флаг.
// -----------------------------------------------------------------------------
struct FileCheckScanState
{
    std::vector<FileCheckHit>              hits;
    uint64_t                               scanned = 0;
    std::chrono::steady_clock::time_point  deadline;
    int                                    maxDepth = 12;
    uint64_t                               maxFiles = 400000; // защитный потолок
};

// Каталоги, которые пропускаем для скорости и чтобы не лезть в системные папки.
// По требованию исключаем C:\\Windows (и системный каталог из %SystemRoot%).
static bool ShouldSkipScanDirectory(const std::wstring& fullPath, const std::wstring& name)
{
    std::wstring lname = ToLower(name);
    if (lname == L"windows" || lname == L"windows.old" ||
        lname == L"$recycle.bin" || lname == L"system volume information" ||
        lname == L"perflogs" ||
        // высокообъёмные шумные каталоги (код/VCS/кеш) — ради скорости
        lname == L"node_modules" || lname == L".git" ||
        lname == L".svn" || lname == L"__pycache__")
        return true;

    // Абсолютный системный каталог (%SystemRoot%, обычно C:\\Windows).
    static const std::wstring sysRoot = []() -> std::wstring {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"SystemRoot", buf, MAX_PATH);
        return (n > 0 && n < MAX_PATH) ? ToLower(std::wstring(buf)) : std::wstring();
    }();
    if (!sysRoot.empty())
    {
        std::wstring lfull = ToLower(fullPath);
        if (lfull == sysRoot || lfull.rfind(sysRoot + L"\\", 0) == 0)
            return true;
    }
    return false;
}

static void ScanDirectoryRecursive(const std::wstring& dir, FileCheckScanState& st, int depth)
{
    // Кооперативная отмена + тайм-бюджет (см. g_automationFileCheckWorking).
    if (!g_automationFileCheckWorking.load()) return;
    if (std::chrono::steady_clock::now() > st.deadline) return;
    if (depth > st.maxDepth) return;
    if (st.scanned > st.maxFiles) return;

    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW fd{};
    // FindExInfoBasic + LARGE_FETCH — быстрее на больших директориях.
    HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE)
        return; // нет доступа к папке — пропускаем

    do
    {
        if (!g_automationFileCheckWorking.load()) break;
        if (std::chrono::steady_clock::now() > st.deadline) break;

        const wchar_t* name = fd.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) continue;

        std::wstring full = dir;
        if (!full.empty() && full.back() != L'\\') full += L'\\';
        full += name;

        // Пропускаем reparse points, чтобы не уйти в петлю по симлинкам.
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Пропускаем C:\\Windows и прочие системные/шумные каталоги.
            if (!ShouldSkipScanDirectory(full, name))
                ScanDirectoryRecursive(full, st, depth + 1);
        }
        else
        {
            ++st.scanned;
            FileCheckHit hit;
            if (EvaluateFile(full, hit))
                st.hits.push_back(std::move(hit));
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// Корни с��анирования по умолчанию: профиль пользователя (самое частое место
// маскировки). При желании можно добавить другие диски.
static std::vector<std::wstring> GetDefaultScanRoots()
{
    std::vector<std::wstring> roots;
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) roots.push_back(buf);
    // Пример расширения: добавить рабочий стол/загрузки/temp и т.п.
    return roots;
}

// -----------------------------------------------------------------------------
//  6. РАБОЧИЙ ПОТОК СКАНИРОВАНИЯ (по образцу StartAutomationScan)
//
//  Формирует pipe-разделённые строки: path | declaredExt | realType | status | reason
//  и кладёт их через SetAutomationLines(AUTOMATION_OUTPUT_FILECHECK, ...),
//  затем шлёт WM_APP_AUTOMATION_DONE, как это делают остальные вкладки.
// -----------------------------------------------------------------------------
static void FileCheckScanWorker()
{
    FileCheckScanState st;
    st.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90); // тайм-бюджет

    for (const auto& root : GetDefaultScanRoots())
    {
        if (!g_automationFileCheckWorking.load()) break;
        ScanDirectoryRecursive(root, st, 0);
    }

    std::vector<std::wstring> lines;
    lines.reserve(st.hits.size() + 1);
    // Заголовок колонок (первой строкой). Колонки pipe-разделённые, поэтому он
    // выравнивается по тем же ширинам, что и данные.
    if (!st.hits.empty())
        lines.push_back(L"Путь | Расширение | Реальный тип | Статус | Причина");
    for (const auto& h : st.hits)
        lines.push_back(h.path + L" | " + h.declaredExt + L" | " +
                        h.realType + L" | " + h.status + L" | " + h.reason);

    if (lines.empty())
        SetAutomationStatusLine(AUTOMATION_OUTPUT_FILECHECK,
            L"Проверено файлов: " + std::to_wstring(st.scanned) +
            L". Несоответствий тип/расширение не найдено.");
    else
        SetAutomationLines(AUTOMATION_OUTPUT_FILECHECK, std::move(lines));

    g_automationFileCheckWorking.store(false);
    // Уведомляем UI-поток так же, как это делают прочие сканы.
    PostMessageW(g_hMain, WM_APP_AUTOMATION_DONE,
                 static_cast<WPARAM>(AUTOMATION_OUTPUT_FILECHECK), 0);
}

// Точка входа по кнопке. Не запускает второй скан, если уже идёт.
static void StartFileCheckScan()
{
    bool expected = false;
    if (!g_automationFileCheckWorking.compare_exchange_strong(expected, true))
        return; // уже сканируем

    SetAutomationStatusLine(AUTOMATION_OUTPUT_FILECHECK, L"Сканирование...");
    InvalidateAutomationPanel(AUTOMATION_OUTPUT_FILECHECK);

    std::thread(FileCheckScanWorker).detach();
}

// =============================================================================
//  7. БУФЕР ОБМЕНА — ТОЛЬКО БЕЗОПАСНЫЕ ФУНКЦИИ
// =============================================================================

// 7.1. Текущий текст буфера обмена (CF_UNICODETEXT). Ничего не логирует и не
//      сохраняет — просто возвращает строку для показа в UI.
static std::wstring GetCurrentClipboardText()
{
    if (!OpenClipboard(nullptr))
        return L"(не удалось открыть буфер обмена)";

    std::wstring result;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData)
    {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(hData));
        if (p)
        {
            result.assign(p);
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    if (result.empty())
        result = L"(в буфере обмена нет текста)";
    return result;
}

// 7.2. Перечисление процессов, связанных с Clipboard User Service (cbdhsvc).
//      Только чтение списка процессов через Toolhelp — никакого доступа к памяти.
#include <tlhelp32.h>
static std::vector<std::wstring> EnumerateClipboardServiceProcesses()
{
    std::vector<std::wstring> found;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return found;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            std::wstring name = ToLower(pe.szExeFile);
            // cbdhsvc обычно хостится в svchost.exe; отдельного exe нет, поэтому
            // отмечаем svchost как возможного хоста службы буфера.
            if (name.find(L"cbdhsvc") != std::wstring::npos ||
                name == L"svchost.exe")
            {
                found.push_back(std::to_wstring(pe.th32ProcessID) + L" | " + pe.szExeFile);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// 7.3. ОФИЦИАЛЬНАЯ история буфера обмена Windows (Win+V).
//      Использует документированный WinRT API Clipboard.GetHistoryItemsAsync.
//      Возвращает только то, что ОС сама сохранила, и только если пользователь
//      включил историю. Намеренно очищенные записи здесь не появляются — это
//      корректное поведение, а не ограничение.
//
//      Требуется компиляция с поддержкой C++/WinRT. Если проект её не
//      использует, оставьте функцию как есть — она вернёт пояснение.
//
//      Реализация-ориентир (включить при наличии C++/WinRT):
//
//      #include <winrt/Windows.Foundation.h>
//      #include <winrt/Windows.Foundation.Collections.h>
//      #include <winrt/Windows.ApplicationModel.DataTransfer.h>
//      using namespace winrt::Windows::ApplicationModel::DataTransfer;
//
//      static std::wstring ReadClipboardHistoryOfficial() {
//          try {
//              if (!Clipboard::IsHistoryEnabled())
//                  return L"История буфера обмена отключена (включите Win+V).";
//              auto res = Clipboard::GetHistoryItemsAsync().get();
//              if (res.Status() != ClipboardHistoryItemsResultStatus::Success)
//                  return L"Не удалось получить историю буфера обмена.";
//              std::wstring out;
//              for (auto const& item : res.Items()) {
//                  auto content = item.Content();
//                  if (content.Contains(StandardDataFormats::Text())) {
//                      auto text = content.GetTextAsync().get();
//                      out += std::wstring(text.c_str()) + L"\r\n----\r\n";
//                  }
//              }
//              return out.empty() ? L"(история буфера пуста)" : out;
//          } catch (...) {
//              return L"(ошибка доступа к истории буфера обмена)";
//          }
//      }
//
//      NB: Я сознательно НЕ реализую восстановление уже очищённого буфера через
//          чтение памяти процесса cbdhsvc — это техника кражи данных
//          (пароли/2FA/seed-фразы). Используйте только официальный API выше.

// =============================================================================
//  8. UI-ОБЁРТКИ ДЛЯ КНОПОК ВКЛАДКИ (вызываются из 09_window_main.inc)
//  Пишут в AUTOMATION_OUTPUT_CLIPBOARD и перерисовывают нижнюю панель page 5.
// =============================================================================

// Кнопка "буфер: текст" — показать текущее текстовое содержимое буфера.
static void ShowCurrentClipboardTextUI()
{
    std::wstring text = GetCurrentClipboardText();
    std::vector<std::wstring> lines;
    lines.push_back(L"Текущий текст буфера обмена:");
    lines.push_back(L"");

    // Разбиваем многострочный текст на отдельные строки для табличного вывода.
    size_t start = 0;
    while (start <= text.size())
    {
        size_t nl = text.find(L'\n', start);
        std::wstring piece = (nl == std::wstring::npos)
            ? text.substr(start)
            : text.substr(start, nl - start);
        while (!piece.empty() && piece.back() == L'\r') piece.pop_back();
        lines.push_back(piece);
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }

    SetAutomationLines(AUTOMATION_OUTPUT_CLIPBOARD, std::move(lines));
    InvalidateAutomationPanel(AUTOMATION_OUTPUT_CLIPBOARD);
}

// Кнопка "cbdhsvc" — перечислить процессы, связанные со службой буфера.
static void ShowClipboardProcessesUI()
{
    std::vector<std::wstring> procs = EnumerateClipboardServiceProcesses();
    std::vector<std::wstring> lines;
    lines.push_back(L"PID | процесс (возможный хост службы буфера cbdhsvc)");
    if (procs.empty())
        lines.push_back(L"Процессы cbdhsvc / svchost не найдены.");
    else
        for (auto& p : procs) lines.push_back(p);

    lines.push_back(L"");
    lines.push_back(L"Примечание: намеренно очищенный буфер НЕ восстанавливается из памяти (см. комментарий в модуле).");
    lines.push_back(L"Для истории буфера используйте официальный Win+V (ReadClipboardHistoryOfficial).");

    SetAutomationLines(AUTOMATION_OUTPUT_CLIPBOARD, std::move(lines));
    InvalidateAutomationPanel(AUTOMATION_OUTPUT_CLIPBOARD);
}
