#include "diagnostics.h"

#include <Windows.h>
#include <shellapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace hd
{
namespace
{
constexpr LONGLONG kMaximumLogSize = 2 * 1024 * 1024;
// An SRW lock is safe zero-initialized by contract; a zeroed CRITICAL_SECTION
// crashes EnterCriticalSection on the hardened ntdll of this machine.
SRWLOCK g_log_lock{};

// =====================================================================
// V165 - LE JOURNAL NE DOIT PLUS COUTER D'IMAGES
// =====================================================================
//
// Chaque ligne faisait TROIS acces disque : GetFileAttributesExW pour lire la
// taille, CreateFileW pour ouvrir, CloseHandle pour fermer. Une seule seconde
// mesuree dans le journal du joueur contenait 1473 lignes identiques : cela
// faisait 1473 ouvertures et fermetures de fichier d'affilee, pendant une
// mission. C'est ce qui faisait saccader le jeu.
//
// Trois mesures, aucune ne touche au jeu ni a ce qui est journalise :
//
//   1. le fichier reste OUVERT - une seule ecriture par ligne, plus aucune
//      ouverture ni fermeture;
//   2. le chemin n'est calcule qu'une fois, et la taille n'est verifiee
//      qu'une fois toutes les deux secondes au lieu de chaque ligne;
//   3. deux lignes identiques qui se suivent sont regroupees : on ecrit la
//      premiere, puis on compte les repetitions et on les resume. Une rafale
//      comme celle du hook de degats ne peut donc plus jamais couter des
//      milliers d'ecritures.
//
// Le handle reste partage en lecture : le journal se lit toujours pendant que
// le jeu tourne, et WriteFile est visible immediatement par un lecteur.
std::wstring BuildLogPath();

HANDLE g_log_file = INVALID_HANDLE_VALUE;
wchar_t g_log_path[MAX_PATH]{};
char g_last_message[1536]{};
unsigned g_repeat_count = 0;
ULONGLONG g_last_size_check = 0;
ULONGLONG g_last_write_tick = 0;

void WriteLineLocked(const char* message)
{
    if (g_log_file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    char line[1792]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] "
        "[tick=%llu pid=%lu tid=%lu] %s\r\n",
        local_time.wYear, local_time.wMonth, local_time.wDay,
        local_time.wHour, local_time.wMinute, local_time.wSecond,
        local_time.wMilliseconds,
        static_cast<unsigned long long>(GetTickCount64()),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        message);
    if (length <= 0)
        return;
    DWORD ignored = 0;
    (void)WriteFile(
        g_log_file, line, static_cast<DWORD>(length), &ignored, nullptr);
    g_last_write_tick = GetTickCount64();
}

// Ecrit le resume des repetitions en attente, s'il y en a.
void FlushRepeatsLocked()
{
    if (g_repeat_count == 0)
        return;
    char summary[128]{};
    (void)_snprintf_s(
        summary, sizeof(summary), _TRUNCATE,
        "   (ligne precedente repetee %u fois de suite)", g_repeat_count);
    g_repeat_count = 0;
    WriteLineLocked(summary);
}

bool EnsureLogOpenLocked()
{
    if (g_log_path[0] == L'\0')
    {
        const std::wstring path = BuildLogPath();
        wcsncpy_s(g_log_path, path.c_str(), _TRUNCATE);
    }
    const ULONGLONG now = GetTickCount64();
    // La taille n'est plus interrogee a chaque ligne : une fois toutes les
    // deux secondes suffit largement pour un plafond de deux megaoctets.
    if (g_log_file != INVALID_HANDLE_VALUE &&
        now - g_last_size_check >= 2000)
    {
        g_last_size_check = now;
        LARGE_INTEGER size{};
        if (GetFileSizeEx(g_log_file, &size) &&
            size.QuadPart > kMaximumLogSize)
        {
            CloseHandle(g_log_file);
            g_log_file = INVALID_HANDLE_VALUE;
            (void)DeleteFileW(g_log_path);
        }
    }
    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        g_log_file = CreateFileW(
            g_log_path, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        g_last_size_check = now;
    }
    return g_log_file != INVALID_HANDLE_VALUE;
}

std::wstring BuildLogPath()
{
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, module_path, static_cast<DWORD>(MAX_PATH));
    if (length == 0 || length >= MAX_PATH)
        return L"hdradar_diag.log";

    std::wstring path(module_path, length);
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return L"hdradar_diag.log";
    path.resize(separator + 1);
    path += L"hdradar_diag.log";
    return path;
}
}

std::wstring DiagnosticLogPath()
{
    return BuildLogPath();
}

void StartDiagnosticSession(const char* version)
{
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    g_last_message[0] = '\0';
    g_repeat_count = 0;
    const std::wstring path = BuildLogPath();
    (void)DeleteFileW(path.c_str());
    ReleaseSRWLockExclusive(&g_log_lock);
    LogDiagnostic(
        "========== TEST SESSION START version=%s log_format=2 ==========",
        version ? version : "unknown");
    LogDiagnostic("Journal path initialized (fresh file for this launch).");
}

bool OpenDiagnosticLog()
{
    const std::wstring path = BuildLogPath();
    HANDLE file = CreateFileW(
        path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

void LogDiagnostic(const char* format, ...)
{
    char message[1536]{};
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf_s(
        message, sizeof(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    if (written < 0)
        return;

    AcquireSRWLockExclusive(&g_log_lock);

    if (EnsureLogOpenLocked())
    {
        if (std::strcmp(message, g_last_message) == 0)
        {
            // Meme ligne que la precedente : on compte au lieu d'ecrire.
            // Une rafale ne coute donc plus rien. Si elle dure, on en donne
            // quand meme des nouvelles toutes les cinq secondes.
            ++g_repeat_count;
            if (GetTickCount64() - g_last_write_tick >= 5000)
                FlushRepeatsLocked();
        }
        else
        {
            FlushRepeatsLocked();
            WriteLineLocked(message);
            (void)strcpy_s(g_last_message, sizeof(g_last_message), message);
        }
    }

    ReleaseSRWLockExclusive(&g_log_lock);
}
}
