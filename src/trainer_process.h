#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hd
{
struct RemoteModuleInfo
{
    std::uintptr_t base_address = 0;
    std::size_t image_size = 0;
};

enum class PatternAddressMode
{
    MatchAddress = 0,
    Absolute32 = 1,
    Relative32 = 2
};

class TrainerProcess final
{
public:
    TrainerProcess() = default;
    ~TrainerProcess();

    TrainerProcess(const TrainerProcess&) = delete;
    TrainerProcess& operator=(const TrainerProcess&) = delete;

    bool RefreshConnection(const wchar_t* executable_name);
    void Disconnect();

    [[nodiscard]] bool IsConnected() const;
    [[nodiscard]] bool IsGameWindowActive() const;
    bool ActivateGameWindow() const;
    [[nodiscard]] DWORD ProcessId() const;
    [[nodiscard]] HWND WindowHandle() const;
    [[nodiscard]] DWORD LastCandidateProcessId() const;
    [[nodiscard]] DWORD LastError() const;

    bool GetMainModuleInfo(RemoteModuleInfo& module_info) const;
    bool GetModuleInfo(
        const wchar_t* module_name,
        RemoteModuleInfo& module_info) const;
    [[nodiscard]] std::uintptr_t GetHDTickClassPointerAddress() const;
    [[nodiscard]] std::uintptr_t PatternScan(const char* ida_pattern) const;
    [[nodiscard]] std::uintptr_t ResolvePatternAddress(
        const char* ida_pattern,
        PatternAddressMode mode,
        int operand_offset,
        int final_adjustment = 0) const;

    bool ReadMemory(std::uintptr_t address, void* destination, std::size_t size) const;
    // Probes a call target the game itself would execute: the first few
    // bytes must be readable. This accepts code outside the main module
    // (game DLLs) while rejecting unmapped or data-page garbage.
    [[nodiscard]] bool IsReadableCodeTarget(std::uintptr_t address) const;
    bool WriteMemory(std::uintptr_t address, const void* source, std::size_t size) const;
    bool WriteProtectedMemory(
        std::uintptr_t address,
        const void* source,
        std::size_t size) const;
    [[nodiscard]] std::uintptr_t AllocateRemoteMemory(
        std::size_t size,
        DWORD protection = PAGE_EXECUTE_READWRITE) const;
    bool FreeRemoteMemory(std::uintptr_t address) const;
    bool IsAnyThreadExecutingRange(
        std::uintptr_t address,
        std::size_t size,
        bool& executing) const;
    // V149 - ecrit du code EXECUTABLE en toute securite.
    //
    // Le site de triche du jeu est traverse a chaque image. Y ecrire cinq
    // octets de saut pendant que le processeur les execute est une course :
    // la plupart du temps elle est gagnee, et de temps en temps le jeu execute
    // une instruction a moitie ecrite et s'arrete.
    //
    // Cette fonction suspend TOUS les threads du jeu, verifie qu'aucun ne se
    // trouve dans la zone visee, ecrit, puis les relance. Elle rend faux - sans
    // avoir rien ecrit - si un thread s'y trouve; l'appelant peut reessayer.
    bool PatchCodeSafely(
        std::uintptr_t address,
        const void* source,
        std::size_t size) const;

    template <typename T>
    bool ReadMemory(std::uintptr_t address, T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "ReadMemory requires a trivially copyable type.");
        return ReadMemory(address, &value, sizeof(T));
    }

    template <typename T>
    bool WriteMemory(std::uintptr_t address, const T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "WriteMemory requires a trivially copyable type.");
        return WriteMemory(address, &value, sizeof(T));
    }

    template <typename T>
    bool WriteProtectedMemory(std::uintptr_t address, const T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "WriteProtectedMemory requires a trivially copyable type.");
        return WriteProtectedMemory(address, &value, sizeof(T));
    }

private:
    HANDLE process_ = nullptr;
    HWND window_ = nullptr;
    DWORD process_id_ = 0;
    DWORD last_candidate_process_id_ = 0;
    DWORD last_error_ = ERROR_SUCCESS;
};
}
