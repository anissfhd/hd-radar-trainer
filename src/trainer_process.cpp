#include "trainer_process.h"

#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <vector>

namespace hd
{
namespace
{
// Hidden & Dangerous Deluxe (hde.exe), SHA-256:
// 5D5EED6174658B8FACFBC1251146B109BBB12AD9A119168AA3489F80979D62D0
constexpr std::uintptr_t kHDTickClassPointerRva = 0x0010AD4C;

struct ProcessSearch
{
    DWORD process_id = 0;
    HWND window = nullptr;
};

struct WindowSearch
{
    DWORD process_id = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter)
{
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);

    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
        return TRUE;

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search.process_id)
        return TRUE;

    search.window = window;
    return FALSE;
}

HWND FindWindowForProcess(DWORD process_id)
{
    WindowSearch search{process_id, nullptr};
    EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

ProcessSearch FindProcess(const wchar_t* executable_name)
{
    ProcessSearch first_match{};
    if (!executable_name || *executable_name == L'\0')
        return first_match;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return first_match;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == 0 ||
                entry.th32ProcessID == GetCurrentProcessId() ||
                _wcsicmp(entry.szExeFile, executable_name) != 0)
            {
                continue;
            }

            ProcessSearch match{
                entry.th32ProcessID,
                FindWindowForProcess(entry.th32ProcessID)};
            if (first_match.process_id == 0)
                first_match = match;
            if (match.window)
            {
                first_match = match;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return first_match;
}

bool ParseIdaPattern(const char* text, std::vector<int>& bytes)
{
    bytes.clear();
    if (!text)
        return false;

    const char* cursor = text;
    while (*cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;

        if (*cursor == '\0')
            break;

        if (*cursor == '?')
        {
            ++cursor;
            if (*cursor == '?')
                ++cursor;
            bytes.push_back(-1);
            continue;
        }

        char* end = nullptr;
        const unsigned long value = std::strtoul(cursor, &end, 16);
        if (end == cursor || value > 0xFF)
            return false;

        bytes.push_back(static_cast<int>(value));
        cursor = end;
    }

    return !bytes.empty();
}

bool RegionMatchesPattern(
    const BYTE* region,
    std::size_t offset,
    const std::vector<int>& pattern)
{
    for (std::size_t index = 0; index < pattern.size(); ++index)
    {
        if (pattern[index] >= 0 &&
            region[offset + index] != static_cast<BYTE>(pattern[index]))
        {
            return false;
        }
    }

    return true;
}

std::uintptr_t AddSignedOffset(std::uintptr_t address, std::int64_t offset)
{
    if (offset >= 0)
        return address + static_cast<std::uintptr_t>(offset);

    const std::uint64_t magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1;
    if (magnitude > address)
        return 0;
    return address - static_cast<std::uintptr_t>(magnitude);
}
}

TrainerProcess::~TrainerProcess()
{
    Disconnect();
}

bool TrainerProcess::RefreshConnection(const wchar_t* executable_name)
{
    if (process_)
    {
        if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
        {
            // Re-elect the window whenever the cached one stops being a
            // visible, unowned top-level window -- not only when it has been
            // destroyed. Attaching to hde.exe while it is still starting can
            // otherwise latch a splash or setup window that stays a valid
            // HWND for the whole session, and every keystroke trigger and
            // foreground check silently targets the wrong window.
            if (!IsWindow(window_) || !IsWindowVisible(window_) ||
                GetWindow(window_, GW_OWNER) != nullptr)
            {
                const HWND elected = FindWindowForProcess(process_id_);
                if (elected)
                    window_ = elected;
            }
            return true;
        }

        Disconnect();
    }

    last_error_ = ERROR_SUCCESS;
    const ProcessSearch search = FindProcess(executable_name);
    last_candidate_process_id_ = search.process_id;
    if (last_candidate_process_id_ == 0)
        return false;

    process_ = OpenProcess(PROCESS_ALL_ACCESS, FALSE, last_candidate_process_id_);
    if (!process_)
    {
        last_error_ = GetLastError();
        return false;
    }

    process_id_ = last_candidate_process_id_;
    window_ = search.window;
    return true;
}

void TrainerProcess::Disconnect()
{
    if (process_)
    {
        CloseHandle(process_);
        process_ = nullptr;
    }

    process_id_ = 0;
    window_ = nullptr;
}

bool TrainerProcess::IsConnected() const
{
    return process_ != nullptr;
}

bool TrainerProcess::IsGameWindowActive() const
{
    if (!process_ || !IsWindow(window_))
        return false;

    DWORD foreground_process_id = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process_id);
    return foreground_process_id == process_id_;
}

bool TrainerProcess::ActivateGameWindow() const
{
    if (!process_ || !IsWindow(window_))
        return false;

    if (IsIconic(window_))
        ShowWindow(window_, SW_RESTORE);

    SetForegroundWindow(window_);
    return IsGameWindowActive();
}

DWORD TrainerProcess::ProcessId() const
{
    return process_id_;
}

HWND TrainerProcess::WindowHandle() const
{
    return window_;
}

DWORD TrainerProcess::LastCandidateProcessId() const
{
    return last_candidate_process_id_;
}

DWORD TrainerProcess::LastError() const
{
    return last_error_;
}

bool TrainerProcess::GetMainModuleInfo(RemoteModuleInfo& module_info) const
{
    module_info = {};
    if (!process_ || process_id_ == 0)
        return false;

    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id_);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    MODULEENTRY32W module_entry{};
    module_entry.dwSize = sizeof(module_entry);
    const BOOL found = Module32FirstW(snapshot, &module_entry);
    CloseHandle(snapshot);
    if (!found)
        return false;

    module_info.base_address = reinterpret_cast<std::uintptr_t>(module_entry.modBaseAddr);
    module_info.image_size = module_entry.modBaseSize;
    return module_info.base_address != 0 && module_info.image_size != 0;
}

bool TrainerProcess::GetModuleInfo(
    const wchar_t* module_name,
    RemoteModuleInfo& module_info) const
{
    module_info = {};
    if (!process_ || process_id_ == 0 || !module_name || !*module_name)
        return false;

    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id_);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    MODULEENTRY32W module_entry{};
    module_entry.dwSize = sizeof(module_entry);
    BOOL found = Module32FirstW(snapshot, &module_entry);
    while (found && _wcsicmp(module_entry.szModule, module_name) != 0)
        found = Module32NextW(snapshot, &module_entry);
    CloseHandle(snapshot);
    if (!found)
        return false;

    module_info.base_address =
        reinterpret_cast<std::uintptr_t>(module_entry.modBaseAddr);
    module_info.image_size = module_entry.modBaseSize;
    return module_info.base_address != 0 && module_info.image_size != 0;
}

std::uintptr_t TrainerProcess::GetHDTickClassPointerAddress() const
{
    RemoteModuleInfo module{};
    if (!GetMainModuleInfo(module) ||
        kHDTickClassPointerRva + sizeof(std::uint32_t) > module.image_size)
    {
        return 0;
    }

    return module.base_address + kHDTickClassPointerRva;
}

std::uintptr_t TrainerProcess::PatternScan(const char* ida_pattern) const
{
    std::vector<int> pattern;
    if (!ParseIdaPattern(ida_pattern, pattern))
        return 0;

    RemoteModuleInfo module{};
    if (!GetMainModuleInfo(module) || pattern.size() > module.image_size)
        return 0;

    const std::uintptr_t module_end = module.base_address + module.image_size;
    std::uintptr_t address = module.base_address;
    while (address < module_end)
    {
        MEMORY_BASIC_INFORMATION memory_info{};
        if (VirtualQueryEx(
                process_,
                reinterpret_cast<const void*>(address),
                &memory_info,
                sizeof(memory_info)) == 0)
        {
            break;
        }

        const std::uintptr_t region_base = reinterpret_cast<std::uintptr_t>(
            memory_info.BaseAddress);
        const std::uintptr_t region_end = region_base + memory_info.RegionSize;
        const std::uintptr_t scan_begin = (std::max)(address, module.base_address);
        const std::uintptr_t scan_end = (std::min)(region_end, module_end);

        const bool readable = memory_info.State == MEM_COMMIT &&
            (memory_info.Protect & PAGE_GUARD) == 0 &&
            (memory_info.Protect & PAGE_NOACCESS) == 0;
        if (readable && scan_end > scan_begin)
        {
            const std::size_t region_size = scan_end - scan_begin;
            std::vector<BYTE> region(region_size);
            SIZE_T bytes_read = 0;
            ReadProcessMemory(
                process_,
                reinterpret_cast<const void*>(scan_begin),
                region.data(),
                region.size(),
                &bytes_read);

            if (bytes_read >= pattern.size())
            {
                const std::size_t last_offset = bytes_read - pattern.size();
                for (std::size_t offset = 0; offset <= last_offset; ++offset)
                {
                    if (RegionMatchesPattern(region.data(), offset, pattern))
                        return scan_begin + offset;
                }
            }
        }

        if (region_end <= address)
            break;
        address = region_end;
    }

    return 0;
}

std::uintptr_t TrainerProcess::ResolvePatternAddress(
    const char* ida_pattern,
    PatternAddressMode mode,
    int operand_offset,
    int final_adjustment) const
{
    const std::uintptr_t match = PatternScan(ida_pattern);
    if (match == 0)
        return 0;

    const std::uintptr_t operand_address = AddSignedOffset(match, operand_offset);
    if (operand_address == 0)
        return 0;

    std::uintptr_t resolved_address = 0;
    switch (mode)
    {
    case PatternAddressMode::MatchAddress:
        resolved_address = operand_address;
        break;

    case PatternAddressMode::Absolute32:
    {
        std::uint32_t absolute_address = 0;
        if (!ReadMemory(operand_address, absolute_address))
            return 0;
        resolved_address = static_cast<std::uintptr_t>(absolute_address);
        break;
    }

    case PatternAddressMode::Relative32:
    {
        std::int32_t displacement = 0;
        if (!ReadMemory(operand_address, displacement))
            return 0;
        resolved_address = AddSignedOffset(
            operand_address + sizeof(displacement), displacement);
        break;
    }

    default:
        return 0;
    }

    return AddSignedOffset(resolved_address, final_adjustment);
}

bool TrainerProcess::ReadMemory(
    std::uintptr_t address, void* destination, std::size_t size) const
{
    if (!process_ || !destination || size == 0)
        return false;

    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
        process_,
        reinterpret_cast<const void*>(address),
        destination,
        size,
        &bytes_read) != FALSE && bytes_read == size;
}

bool TrainerProcess::IsReadableCodeTarget(std::uintptr_t address) const
{
    if (!process_ || address < 0x10000U || address > 0x7FFF'FFFFU)
        return false;
    std::array<std::uint8_t, 8> probe{};
    return ReadMemory(address, probe.data(), probe.size());
}

bool TrainerProcess::WriteMemory(
    std::uintptr_t address, const void* source, std::size_t size) const
{
    if (!process_ || !source || size == 0)
        return false;

    SIZE_T bytes_written = 0;
    return WriteProcessMemory(
        process_,
        reinterpret_cast<void*>(address),
        source,
        size,
        &bytes_written) != FALSE && bytes_written == size;
}

bool TrainerProcess::WriteProtectedMemory(
    std::uintptr_t address,
    const void* source,
    std::size_t size) const
{
    if (!process_ || address == 0 || !source || size == 0)
        return false;

    DWORD old_protection = 0;
    if (!VirtualProtectEx(
            process_,
            reinterpret_cast<void*>(address),
            size,
            PAGE_EXECUTE_READWRITE,
            &old_protection))
    {
        return false;
    }

    const bool written = WriteMemory(address, source, size);
    DWORD ignored = 0;
    const bool protection_restored = VirtualProtectEx(
        process_,
        reinterpret_cast<void*>(address),
        size,
        old_protection,
        &ignored) != FALSE;
    const bool cache_flushed = written &&
        FlushInstructionCache(
            process_, reinterpret_cast<void*>(address), size) != FALSE;
    return written && protection_restored && cache_flushed;
}

std::uintptr_t TrainerProcess::AllocateRemoteMemory(
    std::size_t size, DWORD protection) const
{
    if (!process_ || size == 0)
        return 0;
    return reinterpret_cast<std::uintptr_t>(VirtualAllocEx(
        process_, nullptr, size, MEM_COMMIT | MEM_RESERVE, protection));
}

bool TrainerProcess::FreeRemoteMemory(std::uintptr_t address) const
{
    return process_ && address != 0 &&
        VirtualFreeEx(
            process_, reinterpret_cast<void*>(address), 0, MEM_RELEASE) != FALSE;
}


bool TrainerProcess::PatchCodeSafely(
    std::uintptr_t address,
    const void* source,
    std::size_t size) const
{
    if (!process_ || process_id_ == 0 || address == 0 || source == nullptr ||
        size == 0 ||
        address > (std::numeric_limits<std::uintptr_t>::max)() - size)
    {
        return false;
    }

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    // 1. Suspendre TOUS les threads du jeu, et les garder suspendus.
    std::vector<HANDLE> suspended;
    bool ok = true;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID != process_id_)
                continue;
            const HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION,
                FALSE,
                entry.th32ThreadID);
            if (!thread)
            {
                ok = false;
                break;
            }
            if (SuspendThread(thread) == static_cast<DWORD>(-1))
            {
                CloseHandle(thread);
                ok = false;
                break;
            }
            suspended.push_back(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    else
    {
        ok = false;
    }
    CloseHandle(snapshot);

    // 2. Aucun d'eux ne doit se trouver dans les octets qu'on va remplacer.
    //    On elargit d'un peu la zone examinee : un thread arrete juste avant
    //    la zone pourrait y entrer des qu'il repart, alors qu'on n'aurait pas
    //    fini d'ecrire. Quinze octets couvrent largement une instruction.
    if (ok && !suspended.empty())
    {
        constexpr std::uintptr_t kApproachMargin = 15;
        const std::uintptr_t low =
            address >= kApproachMargin ? address - kApproachMargin : 0;
        const std::uintptr_t high = address + size;
        for (const HANDLE thread : suspended)
        {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread, &context))
            {
                ok = false;
                break;
            }
#if defined(_M_IX86)
            const std::uintptr_t instruction = context.Eip;
#else
            const std::uintptr_t instruction = context.Rip;
#endif
            if (instruction >= low && instruction < high)
            {
                ok = false;   // il est dedans, ou juste devant : on renonce
                break;
            }
        }
    }
    else if (suspended.empty())
    {
        ok = false;
    }

    // 3. Ecrire, puis relancer tout le monde dans tous les cas.
    const bool written = ok && WriteProtectedMemory(address, source, size);
    for (const HANDLE thread : suspended)
    {
        (void)ResumeThread(thread);
        CloseHandle(thread);
    }
    return written;
}

bool TrainerProcess::IsAnyThreadExecutingRange(
    std::uintptr_t address,
    std::size_t size,
    bool& executing) const
{
    executing = false;
    if (!process_ || process_id_ == 0 || address == 0 || size == 0 ||
        address > (std::numeric_limits<std::uintptr_t>::max)() - size)
    {
        return false;
    }

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    bool inspected_any = false;
    bool inspection_ok = true;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID != process_id_)
                continue;
            const HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION,
                FALSE,
                entry.th32ThreadID);
            if (!thread)
            {
                inspection_ok = false;
                break;
            }
            const DWORD previous_suspend_count = SuspendThread(thread);
            if (previous_suspend_count == static_cast<DWORD>(-1))
            {
                CloseHandle(thread);
                inspection_ok = false;
                break;
            }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            const bool got_context = GetThreadContext(thread, &context) != FALSE;
            (void)ResumeThread(thread);
            CloseHandle(thread);
            if (!got_context)
            {
                inspection_ok = false;
                break;
            }
            inspected_any = true;
#if defined(_M_IX86)
            const std::uintptr_t instruction = context.Eip;
#else
            const std::uintptr_t instruction = context.Rip;
#endif
            if (instruction >= address && instruction < address + size)
            {
                executing = true;
                break;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    else
    {
        inspection_ok = false;
    }
    CloseHandle(snapshot);
    return inspection_ok && inspected_any;
}
}
